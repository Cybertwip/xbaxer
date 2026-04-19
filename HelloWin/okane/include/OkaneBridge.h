#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Menu / miner bridge types ---------- */

/* Menu screens presented by the Go layer. */
#define OKANE_MENU_MAIN    0
#define OKANE_MENU_MINING  1
#define OKANE_MENU_STOPPED 2

/* Maximum number of selectable items the menu can expose to the renderer. */
#define OKANE_MAX_MENU_ITEMS  6
/* Maximum length of a single menu-item label (including NUL). */
#define OKANE_MAX_LABEL_LEN  64

typedef struct OkaneMenuItem
{
    char label[OKANE_MAX_LABEL_LEN];
    uint32_t highlighted; /* 1 = currently selected */
} OkaneMenuItem;

typedef struct OkaneMinerStatus
{
    uint32_t running;          /* 1 = mining active */
    double   hashRate;         /* H/s */
    char     hashRateText[32];
    uint64_t blocksFound;
    char     poolUrl[128];
    char     poolStatusText[64];
    char     walletAddr[128];
    char     lastError[256];
} OkaneMinerStatus;

typedef struct OkaneFrameState
{
    /* --- visual state (triangle demo) --- */
    float clearColor[4];
    float triangleColor[4];
    float triangleScale;
    float triangleOffsetX;
    float triangleOffsetY;
    float triangleRotationRadians;
    float trianglePulse;
    uint32_t viewportWidth;
    uint32_t viewportHeight;
    uint64_t frameIndex;

    /* --- menu state --- */
    uint32_t       menuScreen;
    uint32_t       menuItemCount;
    OkaneMenuItem  menuItems[OKANE_MAX_MENU_ITEMS];
    char           menuTitle[OKANE_MAX_LABEL_LEN];

    /* --- miner telemetry --- */
    OkaneMinerStatus minerStatus;
} OkaneFrameState;

#ifndef OKANE_BUILDING_CGO
extern void OkaneApp_Initialize(void);
extern void OkaneApp_Shutdown(void);
extern void OkaneApp_SetViewport(uint32_t width, uint32_t height);
extern void OkaneApp_Update(float totalSeconds, float deltaSeconds);
extern void OkaneApp_GetFrameState(OkaneFrameState *outState);

/* Load mining configuration from a CSV file (key,value rows).
   Must be called after Initialize but before the first Update. */
extern void OkaneApp_LoadConfig(char *path);

/* Menu interaction — called from the C++ input handler. */
extern void OkaneApp_MenuUp(void);
extern void OkaneApp_MenuDown(void);
extern void OkaneApp_MenuSelect(void);
#endif /* OKANE_BUILDING_CGO */

/* ---------- GPU compute hasher bridge ---------- */

typedef void *OkaneGpuHandle;

/* Create a GPU hasher instance. Returns NULL on failure. */
OkaneGpuHandle OkaneGpu_Create(void);

/* Destroy a previously created GPU hasher. */
void OkaneGpu_Destroy(OkaneGpuHandle handle);

/* Dispatch a full-nonce-range search on the GPU.
   header: 80-byte block header (nonce field will be varied by the shader).
   target: 32-byte target hash.
   outHeader: receives the 80-byte winning header if found.
   Returns 1 if a valid nonce was found, 0 otherwise. */
int OkaneGpu_Process(OkaneGpuHandle handle,
                     const uint8_t *header,
                     const uint8_t *target,
                     uint8_t *outHeader);

#ifdef __cplusplus
}
#endif
