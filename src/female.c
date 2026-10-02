#include "female.h"

enum { H_MEAN, H_IY, H_IH, H_EI, H_EH, H_AE, H_AH, H_AW, H_OA, H_OO, H_UW, H_UH, H_ER };

const unsigned short femaleRatio1000[13][3] =
{
    {1176, 1162, 1138},  // mean over all 12 vowels (consonants)
    {1276, 1189, 1124},  // iy  heed
    {1128, 1165, 1138},  // ih  hid
    {1125, 1209, 1133},  // ei  hayed
    {1237, 1144, 1134},  // eh  head
    {1142, 1210, 1145},  // ae  had
    {1218, 1166, 1117},  // ah  hod
    {1226, 1161, 1120},  // aw  hawed
    {1116, 1137, 1150},  // oa  hoed
    {1105, 1094, 1162},  // oo  hood
    {1211, 1114, 1161},  // uw  who'd
    {1223, 1198, 1139},  // uh  hud
    {1103, 1152, 1128},  // er  heard
};

const unsigned char femalePhonemeVowel[FEMALE_NPHONEMES] =
{
    H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN,
    H_IY,   // 5  IY
    H_IH,   // 6  IH
    H_EH,   // 7  EH
    H_AE,   // 8  AE
    H_AH,   // 9  AA  (hod)
    H_UH,   // 10 AH  (hud)
    H_AW,   // 11 AO  (hawed)
    H_OO,   // 12 UH  (hood)
    H_UH,   // 13 AX  (schwa ~ hud)
    H_IH,   // 14 IX
    H_ER,   // 15 ER
    H_UW,   // 16 UX
    H_OA,   // 17 OH
    H_ER,   // 18 RX
    H_MEAN, // 19 LX
    H_OO,   // 20 WX  (diphthong off-glide)
    H_IH,   // 21 YX  (diphthong off-glide)
    H_UW,   // 22 WH
    H_ER,   // 23 R*
    H_MEAN, // 24 L*
    H_UW,   // 25 W*
    H_IY,   // 26 Y*
    H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN,                 // 27-31 M N NX DX Q
    H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, // 32-41
    H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN,         // 42-47
    H_EI,   // 48 EY
    H_AH,   // 49 AY
    H_AW,   // 50 OY
    H_AH,   // 51 AW
    H_OA,   // 52 OW
    H_UW,   // 53 UW
    // 54-80 stops and the UL/UM/UN endings
    H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN,
    H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN,
    H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN, H_MEAN,
};
