/* gta5-verify — proves the patched D3D12 chain before the game starts.
 *
 * Path under test: d3d12core.dll (agility proxy) -> D3D12GetInterface ->
 * SetSDKVersion -> CreateDeviceFactory -> CreateDevice -> queue -> D3DMetal dxgi
 * factory -> flip-discard swapchain -> present. Prints one line per step and a
 * final PASS/FAIL. The exe name must start with "gta5" so the proxy routes the
 * process to Apple's d3d12_gptk instead of the stock fallback.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *L;
static int failures;
static void say(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(L, fmt, a); fflush(L); va_end(a); }
static void step(const char *what, HRESULT hr) {
  say("%-42s %s 0x%08lX\n", what, SUCCEEDED(hr) ? "ok  " : "FAIL", (unsigned long)hr);
  if (FAILED(hr)) failures++;
}
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static const GUID CLSID_SDKCFG = {0x7cda6aca,0xa03e,0x49c8,{0x94,0x58,0x03,0x34,0xd2,0x0e,0x07,0xce}};
static const GUID IID_SDKCFG1  = {0x8aaf9303,0xad25,0x48b9,{0x9a,0x57,0xd9,0xc3,0x7e,0x00,0x9d,0x9f}};
static const GUID IID_FACTORY  = {0x61f307d3,0xd16c,0x4e7c,{0x83,0x74,0x3a,0xcd,0x2c,0x05,0x58,0x21}};
typedef HRESULT (WINAPI *gi_t)(const GUID *, const GUID *, void **);

int main(void) {
  HMODULE core; gi_t gi; HRESULT hr;
  void *cfg = NULL, *fac = NULL; ID3D12Device *dev = NULL;
  ID3D12CommandQueue *q = NULL; IDXGIFactory4 *xf = NULL; IDXGISwapChain1 *sc = NULL;
  L = fopen("verify.log", "w"); if (!L) L = stdout; setvbuf(L, NULL, _IONBF, 0);

  say("gta5-enhanced-gptk verification\n");
  core = LoadLibraryA("d3d12core.dll");
  say("%-42s %s\n", "load d3d12core.dll", core ? "ok" : "FAIL");
  if (!core) { say("\nFAIL\n"); return 1; }

  {
    void *v = (void *)GetProcAddress(core, "D3D12SDKVersion");
    say("%-42s %s\n", "d3d12core!D3D12SDKVersion", v ? "ok" : "FAIL");
    if (v) say("  SDK version = %u\n", *(unsigned int *)v);
    if (!v) failures++;
  }
  gi = (gi_t)GetProcAddress(core, "D3D12GetInterface");
  say("%-42s %s\n", "d3d12core!D3D12GetInterface", gi ? "ok" : "FAIL");
  if (!gi) { say("\nFAIL\n"); return 1; }

  hr = gi(&CLSID_SDKCFG, &IID_SDKCFG1, &cfg);
  step("D3D12GetInterface(SDKConfiguration)", hr);
  if (cfg) {
    void **vt = *(void ***)cfg;
    HRESULT (WINAPI *setver)(void *, UINT, const char *) = (HRESULT (WINAPI *)(void *, UINT, const char *))vt[3];
    HRESULT (WINAPI *mkfac)(void *, UINT, const char *, const GUID *, void **) =
        (HRESULT (WINAPI *)(void *, UINT, const char *, const GUID *, void **))vt[4];
    step("SetSDKVersion(619)", setver(cfg, 619, "."));
    step("CreateDeviceFactory(619)", mkfac(cfg, 619, ".", &IID_FACTORY, &fac));
  }
  if (fac) {
    void **vt = *(void ***)fac;
    HRESULT (WINAPI *cdev)(void *, void *, UINT, const GUID *, void **) =
        (HRESULT (WINAPI *)(void *, void *, UINT, const GUID *, void **))vt[9];
    step("DeviceFactory::CreateDevice(12_0)", cdev(fac, NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&dev));
  }
  if (dev) {
    D3D12_FEATURE_DATA_SHADER_MODEL sm; sm.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    D3D12_COMMAND_QUEUE_DESC qd; ZeroMemory(&qd, sizeof(qd)); qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
    step("CheckFeatureSupport(SHADER_MODEL)", hr);
    say("  highest shader model = 0x%X\n", sm.HighestShaderModel);
    step("CreateCommandQueue(DIRECT)", ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&q));
  }
  step("CreateDXGIFactory2", CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&xf));
  if (q && xf) {
    WNDCLASSEXW wc; HWND w; DXGI_SWAP_CHAIN_DESC1 sd;
    ZeroMemory(&wc, sizeof(wc)); wc.cbSize = sizeof(wc); wc.lpfnWndProc = wp; wc.lpszClassName = L"verify";
    RegisterClassExW(&wc);
    w = CreateWindowExW(0, L"verify", L"verify", WS_OVERLAPPEDWINDOW, 0, 0, 640, 360, NULL, NULL, NULL, NULL);
    ZeroMemory(&sd, sizeof(sd)); sd.Width = 640; sd.Height = 360; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = IDXGIFactory2_CreateSwapChainForHwnd((IDXGIFactory2 *)xf, (IUnknown *)q, w, &sd, NULL, NULL, &sc);
    step("CreateSwapChainForHwnd(flip-discard)", hr);
    if (sc) step("Present", IDXGISwapChain1_Present(sc, 0, 0));
    if (dev) step("GetDeviceRemovedReason", ID3D12Device_GetDeviceRemovedReason(dev));
  }
  say("\n%s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
