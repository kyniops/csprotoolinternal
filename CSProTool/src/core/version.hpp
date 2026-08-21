#pragma once

// Numero de build incremente a chaque DLL livree dans tools/.
#define CSPT_BUILD 307
#define CSPT_STR2(x) #x
#define CSPT_STR(x) CSPT_STR2(x)
#define CSPT_VERSION "v" CSPT_STR(CSPT_BUILD)
