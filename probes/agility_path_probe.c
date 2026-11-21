/* Agility path probe: D3D12GetInterface(CLSID_D3D12SDKConfiguration) ->
 * SetSDKVersion(618) -> CreateDeviceFactory -> factory->CreateDevice -> queue -> swapchain.
 * This is the path a 2025 title uses instead of calling D3D12CreateDevice directly. */
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdarg.h>

static FILE *L;
static void say(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(L, fmt, a); fflush(L); va_end(a); }
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static const GUID CLSID_SDKCFG = {0x7cda6aca, 0xa03e, 0x49c8, {0x94,0x58,0x03,0x34,0xd2,0x0e,0x07,0xce}};
static const GUID IID_SDKCFG1  = {0x8aaf9303, 0xad25, 0x48b9, {0x9a,0x57,0xd9,0xc3,0x7e,0x00,0x9d,0x9f}};
static const GUID IID_FACTORY  = {0x61f307d3, 0xd16c, 0x4e7c, {0x83,0x74,0x3a,0xcd,0x2c,0x05,0x58,0x21}};

typedef HRESULT (WINAPI *get_iface_t)(const GUID *, const GUID *, void **);

int main(void) {
  HMODULE d3d12 = LoadLibraryA("d3d12.dll");
  get_iface_t gi = d3d12 ? (get_iface_t)GetProcAddress(d3d12, "D3D12GetInterface") : NULL;
  void *cfg = NULL, *fac = NULL, *dev = NULL, *q = NULL, *sc = NULL;
  HRESULT hr;
  L = fopen("C:\\gptktest\\agilitypath.log", "w");
  if (!L) L = stdout;
  setvbuf(L, NULL, _IONBF, 0);

  say("d3d12=%p GetInterface=%p\n", (void *)d3d12, (void *)gi);
  if (!gi) return 0;
  hr = gi(&CLSID_SDKCFG, &IID_SDKCFG1, &cfg);
  say("SDKConfiguration1 hr=0x%08lX cfg=%p\n", (unsigned long)hr, cfg);
  if (cfg) {
    void **vt = *(void ***)cfg;
    HRESULT (WINAPI *setver)(void *, UINT, const char *) = (HRESULT (WINAPI *)(void *, UINT, const char *))vt[3];
    HRESULT (WINAPI *mkfac)(void *, UINT, const char *, const GUID *, void **) =
        (HRESULT (WINAPI *)(void *, UINT, const char *, const GUID *, void **))vt[4];
    hr = setver(cfg, 618, ".");
    say("SetSDKVersion(618) hr=0x%08lX\n", (unsigned long)hr);
    hr = mkfac(cfg, 618, ".", &IID_FACTORY, &fac);
    say("CreateDeviceFactory hr=0x%08lX fac=%p\n", (unsigned long)hr, fac);
  }
  if (fac) {
    void **vt = *(void ***)fac;
    HRESULT (WINAPI *init)(void *) = (HRESULT (WINAPI *)(void *))vt[3];
    HRESULT (WINAPI *apply)(void *) = (HRESULT (WINAPI *)(void *))vt[4];
    HRESULT (WINAPI *cdev)(void *, void *, UINT, const GUID *, void **) =
        (HRESULT (WINAPI *)(void *, void *, UINT, const GUID *, void **))vt[9];
    say("factory InitializeFromGlobalState hr=0x%08lX\n", (unsigned long)init(fac));
    say("factory ApplyToGlobalState hr=0x%08lX\n", (unsigned long)apply(fac));
    hr = cdev(fac, NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, &dev);
    say("factory CreateDevice(12_0) hr=0x%08lX dev=%p\n", (unsigned long)hr, dev);
  }
  if (dev) {
    D3D12_COMMAND_QUEUE_DESC qd; ZeroMemory(&qd, sizeof(qd)); qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = ID3D12Device_CreateCommandQueue((ID3D12Device *)dev, &qd, &IID_ID3D12CommandQueue, (void **)&q);
    say("CreateCommandQueue hr=0x%08lX q=%p\n", (unsigned long)hr, q);
  }
  if (q) {
    IDXGIFactory4 *x4 = NULL;
    hr = CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void **)&x4);
    say("CreateDXGIFactory2 hr=0x%08lX\n", (unsigned long)hr);
    if (x4) {
      WNDCLASSEXW wc; HWND w;
      ZeroMemory(&wc, sizeof(wc)); wc.cbSize = sizeof(wc); wc.lpfnWndProc = wp; wc.lpszClassName = L"ap";
      RegisterClassExW(&wc);
      w = CreateWindowExW(0, L"ap", L"p", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 720, NULL, NULL, NULL, NULL);
      {
        DXGI_SWAP_CHAIN_DESC1 sd; ZeroMemory(&sd, sizeof(sd));
        sd.Width = 1280; sd.Height = 720; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 3; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = IDXGIFactory2_CreateSwapChainForHwnd((IDXGIFactory2 *)x4, (IUnknown *)q, w, &sd, NULL, NULL, (IDXGISwapChain1 **)&sc);
        say("CreateSwapChainForHwnd hr=0x%08lX sc=%p\n", (unsigned long)hr, sc);
      }
    }
  }
  say("done\n");
  return 0;
}
