#include "features/avatars.hpp"
#include "hooks/present.hpp"
#include "common.hpp"
#include <d3d11.h>
#include <wincodec.h>
#include <winhttp.h>
#include <vector>
#include <mutex>
#include <string>
#include <cstring>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace avatars {
    enum St : int { Empty = 0, Queued = 1, Pixels = 2, Ready = 3, Fail = 4 };

    struct Slot {
        uint64_t id = 0;
        St st = Empty;
        std::vector<uint8_t> rgba;
        int w = 0;
        int h = 0;
        ID3D11ShaderResourceView* srv = nullptr;
        DWORD fail_at = 0;
    };

    static constexpr int k_max = 96;
    static Slot g_slots[k_max]{};
    static std::mutex g_mu;
    static HANDLE g_thr = nullptr;
    static bool g_run = false;
    static ID3D11Device* g_dev = nullptr;

    static Slot* find(uint64_t id) {
        Slot* empty = nullptr;
        for (int i = 0; i < k_max; ++i) {
            if (g_slots[i].id == id) return &g_slots[i];
            if (!empty && g_slots[i].st == Empty) empty = &g_slots[i];
        }
        return empty;
    }

    static bool http_get(const wchar_t* host, const wchar_t* path, std::vector<uint8_t>& out) {
        out.clear();
        HINTERNET ses = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses) return false;
        WinHttpSetTimeouts(ses, 4000, 4000, 4000, 6000);
        HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!con) { WinHttpCloseHandle(ses); return false; }
        HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!req) {
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return false;
        }
        bool ok = false;
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(req, nullptr)) {
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
                const size_t old = out.size();
                if (old + avail > 2 * 1024 * 1024) break;
                out.resize(old + avail);
                DWORD got = 0;
                if (!WinHttpReadData(req, out.data() + old, avail, &got)) break;
                out.resize(old + got);
                ok = true;
            }
        }
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return ok && !out.empty();
    }

    static bool parse_url(const std::string& url, std::wstring& host, std::wstring& path) {
        const char* s = url.c_str();
        if (std::strncmp(s, "https://", 8) == 0) s += 8;
        else if (std::strncmp(s, "http://", 7) == 0) s += 7;
        else return false;
        const char* slash = std::strchr(s, '/');
        std::string h = slash ? std::string(s, slash) : std::string(s);
        std::string p = slash ? slash : "/";
        if (h.empty()) return false;
        host.assign(h.begin(), h.end());
        path.assign(p.begin(), p.end());
        return true;
    }

    static bool parse_avatar(const std::vector<uint8_t>& xml, std::string& url) {
        if (xml.empty()) return false;
        std::string s(reinterpret_cast<const char*>(xml.data()), xml.size());
        const char* keys[] = { "<avatarMedium>", "<avatarmedium>", "<avatarIcon>", "<avatarFull>" };
        for (auto* k : keys) {
            auto pos = s.find(k);
            if (pos == std::string::npos) continue;
            pos += std::strlen(k);
            auto cdata = s.find("<![CDATA[", pos);
            if (cdata != std::string::npos && cdata < pos + 40) {
                cdata += 9;
                auto end = s.find("]]>", cdata);
                if (end == std::string::npos) continue;
                url = s.substr(cdata, end - cdata);
            } else {
                auto end = s.find('<', pos);
                if (end == std::string::npos) continue;
                url = s.substr(pos, end - pos);
            }
            while (!url.empty() && (url.front() == ' ' || url.front() == '\n' || url.front() == '\r'))
                url.erase(url.begin());
            while (!url.empty() && (url.back() == ' ' || url.back() == '\n' || url.back() == '\r'))
                url.pop_back();
            if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0)
                return true;
        }
        return false;
    }

    static bool decode_image(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& rgba, int& w, int& h) {
        IWICImagingFactory* fac = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&fac))) || !fac)
            return false;

        IWICStream* stream = nullptr;
        if (FAILED(fac->CreateStream(&stream)) || !stream) {
            fac->Release();
            return false;
        }
        if (FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))) {
            stream->Release();
            fac->Release();
            return false;
        }

        IWICBitmapDecoder* dec = nullptr;
        HRESULT hr = fac->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec);
        stream->Release();
        if (FAILED(hr) || !dec) {
            fac->Release();
            return false;
        }

        IWICBitmapFrameDecode* frame = nullptr;
        if (FAILED(dec->GetFrame(0, &frame)) || !frame) {
            dec->Release();
            fac->Release();
            return false;
        }

        IWICFormatConverter* conv = nullptr;
        if (FAILED(fac->CreateFormatConverter(&conv)) || !conv) {
            frame->Release();
            dec->Release();
            fac->Release();
            return false;
        }
        hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
        UINT uw = 0, uh = 0;
        conv->GetSize(&uw, &uh);
        if (FAILED(hr) || uw < 8 || uh < 8 || uw > 256 || uh > 256) {
            conv->Release();
            frame->Release();
            dec->Release();
            fac->Release();
            return false;
        }
        w = static_cast<int>(uw);
        h = static_cast<int>(uh);
        rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4ull);
        hr = conv->CopyPixels(nullptr, static_cast<UINT>(w * 4), static_cast<UINT>(rgba.size()), rgba.data());
        conv->Release();
        frame->Release();
        dec->Release();
        fac->Release();
        return SUCCEEDED(hr);
    }

    static void fetch_one(uint64_t id) {
        wchar_t path[96]{};
        swprintf_s(path, L"/profiles/%llu/?xml=1", static_cast<unsigned long long>(id));
        std::vector<uint8_t> xml;
        if (!http_get(L"steamcommunity.com", path, xml))
            return;
        std::string url;
        if (!parse_avatar(xml, url))
            return;
        std::wstring host, pth;
        if (!parse_url(url, host, pth))
            return;
        std::vector<uint8_t> img;
        if (!http_get(host.c_str(), pth.c_str(), img))
            return;
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!decode_image(img, rgba, w, h))
            return;
        std::lock_guard<std::mutex> lk(g_mu);
        Slot* s = find(id);
        if (!s || s->id != id) return;
        s->rgba = std::move(rgba);
        s->w = w;
        s->h = h;
        s->st = Pixels;
    }

    static DWORD WINAPI worker(LPVOID) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (g_run) {
            uint64_t id = 0;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                for (int i = 0; i < k_max; ++i) {
                    if (g_slots[i].st == Queued && g_slots[i].id) {
                        id = g_slots[i].id;
                        break;
                    }
                }
            }
            if (!id) {
                Sleep(120);
                continue;
            }
            fetch_one(id);
            std::lock_guard<std::mutex> lk(g_mu);
            Slot* s = find(id);
            if (s && s->id == id && s->st == Queued) {
                s->st = Fail;
                s->fail_at = GetTickCount();
            }
        }
        CoUninitialize();
        return 0;
    }

    static void ensure_thread() {
        if (g_thr) return;
        g_run = true;
        g_thr = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    }

    static void upload(Slot& s) {
        auto* dev = hooks::d3d_device();
        if (!dev || s.rgba.empty() || s.w <= 0 || s.h <= 0) return;
        if (s.srv) {
            s.srv->Release();
            s.srv = nullptr;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(s.w);
        desc.Height = static_cast<UINT>(s.h);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = s.rgba.data();
        sub.SysMemPitch = static_cast<UINT>(s.w * 4);
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(dev->CreateTexture2D(&desc, &sub, &tex)) || !tex)
            return;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(dev->CreateShaderResourceView(tex, nullptr, &srv))) {
            tex->Release();
            return;
        }
        tex->Release();
        s.srv = srv;
        s.st = Ready;
        s.rgba.clear();
        s.rgba.shrink_to_fit();
        g_dev = dev;
    }

    void request(uint64_t steamid) {
        if (steamid < 76561197960265728ull) return;
        ensure_thread();
        std::lock_guard<std::mutex> lk(g_mu);
        Slot* s = find(steamid);
        if (!s) return;
        if (s->id == steamid) {
            if (s->st == Fail && GetTickCount() - s->fail_at > 60000) {
                s->st = Queued;
            }
            return;
        }
        if (s->srv) { s->srv->Release(); s->srv = nullptr; }
        s->id = steamid;
        s->st = Queued;
        s->rgba.clear();
        s->w = s->h = 0;
    }

    bool get(uint64_t steamid, ImTextureID* tex, int* w, int* h) {
        if (!tex) return false;
        std::lock_guard<std::mutex> lk(g_mu);
        Slot* s = find(steamid);
        if (!s || s->id != steamid || s->st != Ready || !s->srv)
            return false;
        *tex = reinterpret_cast<ImTextureID>(s->srv);
        if (w) *w = s->w;
        if (h) *h = s->h;
        return true;
    }

    void tick() {
        auto* dev = hooks::d3d_device();
        std::lock_guard<std::mutex> lk(g_mu);
        if (dev && g_dev && dev != g_dev) {
            for (int i = 0; i < k_max; ++i) {
                if (g_slots[i].srv) {
                    g_slots[i].srv->Release();
                    g_slots[i].srv = nullptr;
                }
                if (g_slots[i].st == Ready)
                    g_slots[i].st = Fail;
            }
            g_dev = dev;
        }
        for (int i = 0; i < k_max; ++i) {
            if (g_slots[i].st == Pixels)
                upload(g_slots[i]);
        }
    }

    void shutdown() {
        g_run = false;
        if (g_thr) {
            WaitForSingleObject(g_thr, 1500);
            CloseHandle(g_thr);
            g_thr = nullptr;
        }
        std::lock_guard<std::mutex> lk(g_mu);
        for (int i = 0; i < k_max; ++i) {
            if (g_slots[i].srv) {
                g_slots[i].srv->Release();
                g_slots[i].srv = nullptr;
            }
            g_slots[i] = {};
        }
        g_dev = nullptr;
    }
}
