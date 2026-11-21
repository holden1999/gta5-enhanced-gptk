/*
 * d3d12.dll proxy for GTA V Enhanced on D3DMetal.
 *
 * The game asks the DirectX 12 Agility documents through D3D12GetInterface. Apple's D3DMetal
 * 4.0b3 answers every other call but returns E_NOINTERFACE for CLSID_D3D12SDKConfiguration and
 * CLSID_D3D12DeviceFactory, so the game reports ERR_GFX_D3D_NOD3D12 and refuses to start,
 * although D3D12CreateDevice itself works at 12_2.
 *
 * This proxy sits next to the game exe (the application directory is searched first when the
 * override is d3d12=n,b). It supplies a complete ID3D12SDKConfiguration / ID3D12SDKConfiguration1
 * and an ID3D12DeviceFactory with the exact vtable order from d3d12.h (SDK 602), and forwards
 * every real call to the system d3d12.dll, which is D3DMetal.
 *
 * Build: ./build.sh (mingw-w64, linked with -luuid). It is compiled twice:
 *   dist/d3d12core.dll   the Agility core, exports D3D12SDKVersion = 619
 *   dist/d3d12.dll       the same file for the game root
 * Remove the files from the game folder, or run ./uninstall.sh, to revert.
 */
#include <windows.h>

#define LOG_PATH "C:\\agility-proxy.log"

/* The launcher is answered with "no D3D12 here" so its CEF window keeps the D3D11 path. */
static int stub_only = 0;

/* CLSIDs / IIDs from d3d12.h (SDK 602). */
static const GUID CLSID_D3D12SDKConfiguration =
  {0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}};
static const GUID CLSID_D3D12DeviceFactory =
  {0x61f307d3, 0xd16c, 0x4e7c, {0x83, 0x74, 0x3a, 0xcd, 0x2c, 0x05, 0x58, 0x21}};
static const GUID IID_ID3D12SDKConfiguration =
  {0xe9eb5314, 0x33aa, 0x42b2, {0xa7, 0x18, 0xd7, 0x7f, 0x58, 0xb1, 0xf1, 0xc7}};
static const GUID IID_ID3D12SDKConfiguration1 =
  {0x8aaf9303, 0xad25, 0x48b9, {0x9a, 0x57, 0xd9, 0xc3, 0x7e, 0x00, 0x9d, 0x9f}};
static const GUID IID_ID3D12DeviceFactory =
  {0x61f307d3, 0xd16c, 0x4e7c, {0x83, 0x74, 0x3a, 0xcd, 0x2c, 0x05, 0x58, 0x21}};

typedef int D3D12_DEVICE_FACTORY_FLAGS;

__declspec(dllexport) HRESULT WINAPI D3D12GetInterface(const GUID *clsid, const GUID *riid,
                                                       void **object);

typedef HRESULT (WINAPI *create_device_t)(void *, UINT, const GUID *, void **);
typedef HRESULT (WINAPI *get_debug_t)(const GUID *, void **);
typedef HRESULT (WINAPI *get_interface_t)(const GUID *, const GUID *, void **);
typedef HRESULT (WINAPI *enable_exp_t)(UINT, const GUID *, void *, UINT *);
typedef HRESULT (WINAPI *serialize_versioned_t)(const void *, void **, void **);
typedef HRESULT (WINAPI *serialize_root_t)(const void *, UINT, void **, void **);
typedef HRESULT (WINAPI *create_rs_deser_t)(const void *, UINT, const GUID *, void **);

static HMODULE real_module;
static create_device_t real_create_device;
static get_debug_t real_get_debug_interface;
static get_interface_t real_get_interface;
static enable_exp_t real_enable_experimental;
static serialize_versioned_t real_serialize_versioned;
static serialize_root_t real_serialize_root;
static create_rs_deser_t real_create_rs_deserializer;

static void log_line(const char *text) {
  HANDLE file = CreateFileA(LOG_PATH, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
  char line[320];
  DWORD written = 0;
  if (file == INVALID_HANDLE_VALUE) return;
  wsprintfA(line, "[%lu] %s", (unsigned long)GetCurrentProcessId(), text);
  WriteFile(file, line, (DWORD)lstrlenA(line), &written, NULL);
  WriteFile(file, "\r\n", 2, &written, NULL);
  CloseHandle(file);
}

static void log_guid(const char *what, const GUID *g) {
  char line[200];
  wsprintfA(line, "proxy: %s {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", what,
            (unsigned long)g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1],
            g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
  log_line(line);
}

static int guid_equal(const GUID *a, const GUID *b) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  unsigned i;
  for (i = 0; i < sizeof(GUID); i++) {
    if (pa[i] != pb[i]) return 0;
  }
  return 1;
}

static void load_real(void) {
  char self[MAX_PATH];
  char dir[MAX_PATH];
  char path[MAX_PATH];
  char line[MAX_PATH + 64];
  if (real_module) return;

  /* The staging engine writes this proxy as C:\windows\system32\d3d12.dll and keeps the real
   * D3DMetal PE beside it under a name of our own, so the pair never overwrites each other. */
  GetModuleFileNameA(NULL, self, MAX_PATH);
  lstrcpynA(dir, self, MAX_PATH);
  {
    char *slash = dir;
    char *last = dir;
    while (*slash) {
      if (*slash == '\\') last = slash;
      slash++;
    }
    last[1] = 0;
  }
  /* Per-handler routing: the game gets D3DMetal, every other process (the RGL launcher and
     its CEF helper) gets Wine's own D3D12 so it keeps its DXVK path. */
  {
    char host[MAX_PATH];
    const char *base = host;
    char *p;
    int is_game = 0;
    GetModuleFileNameA(NULL, host, MAX_PATH);
    for (p = host; *p; p++)
      if (*p == '\\' || *p == '/') base = p + 1;
    for (p = (char *)base; *p; p++)
      if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    if (strstr(base, "gta5") || strstr(base, "playgtav")) is_game = 1;
    /* launcher: Wine's vkd3d device answers the GPU query; game: Apple D3DMetal */
    {
      char line[MAX_PATH + 96];
      wsprintfA(line, "proxy: host=%s provider=%s", base,
                is_game ? "d3d12_gptk.dll" : "d3d12_stock.dll");
      log_line(line);
    }
    if (stub_only) {
      log_line("proxy: D3D12 not offered to this host");
      return;
    }
    if (is_game) {
      real_module = LoadLibraryExW(L"C:\\windows\\system32\\d3d12_gptk.dll", NULL,
                                   LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!real_module || real_module == GetModuleHandleW(NULL)) {
        char err[80];
        wsprintfA(err, "proxy: gptk load failed, err=%lu", (unsigned long)GetLastError());
        log_line(err);
        real_module = NULL;
      }
    }
    if (!real_module) {
      real_module = LoadLibraryExW(
          L"C:\\windows\\system32\\d3d12_wine.dll", NULL,
          LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!real_module || real_module == GetModuleHandleW(NULL)) real_module = NULL;
    }
    if (!real_module && is_game) {
      real_module = LoadLibraryExW(
          L"C:\\windows\\system32\\d3d12_wine.dll", NULL,
          LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!real_module || real_module == GetModuleHandleW(NULL)) real_module = NULL;
    }
  }
  if (!real_module) {
    log_line("proxy: no real d3d12 (d3d12_gptk.dll / d3d12_stock.dll) in system32");
    return;
  }
  if (GetModuleFileNameA(real_module, self, MAX_PATH)) {
    wsprintfA(line, "proxy: real d3d12 at %s", self);
    log_line(line);
  }

  real_create_device = (create_device_t)GetProcAddress(real_module, "D3D12CreateDevice");
  real_get_debug_interface = (get_debug_t)GetProcAddress(real_module, "D3D12GetDebugInterface");
  real_get_interface = (get_interface_t)GetProcAddress(real_module, "D3D12GetInterface");
  real_enable_experimental =
    (enable_exp_t)GetProcAddress(real_module, "D3D12EnableExperimentalFeatures");
  real_serialize_versioned =
    (serialize_versioned_t)GetProcAddress(real_module, "D3D12SerializeVersionedRootSignature");
  real_serialize_root =
    (serialize_root_t)GetProcAddress(real_module, "D3D12SerializeRootSignature");
  real_create_rs_deserializer =
    (create_rs_deser_t)GetProcAddress(real_module, "D3D12CreateRootSignatureDeserializer");
  {
    char path[MAX_PATH];
    char line[MAX_PATH + 32];
    if (GetModuleFileNameA(real_module, path, MAX_PATH)) {
      wsprintfA(line, "proxy: real d3d12 at %s", path);
      log_line(line);
    }
  }
}

/* ---- shared IUnknown for the three stubs ---- */
typedef struct {
  const void **vtable;
} stub_object;

static ULONG WINAPI stub_add_ref(stub_object *self) { (void)self; return 2; }
static ULONG WINAPI stub_release(stub_object *self) { (void)self; return 1; }

static HRESULT WINAPI sdk_query_interface(stub_object *self, const GUID *riid, void **object) {
  if (!object) return E_POINTER;
  if (!riid) { *object = NULL; return E_INVALIDARG; }
  if (guid_equal(riid, &IID_IUnknown) || guid_equal(riid, &IID_ID3D12SDKConfiguration) ||
      guid_equal(riid, &IID_ID3D12SDKConfiguration1)) {
    *object = self;
    return S_OK;
  }
  *object = NULL;
  return E_NOINTERFACE;
}

static HRESULT WINAPI df_query_interface(stub_object *self, const GUID *riid, void **object) {
  if (!object) return E_POINTER;
  if (!riid) { *object = NULL; return E_INVALIDARG; }
  if (guid_equal(riid, &IID_IUnknown) || guid_equal(riid, &IID_ID3D12DeviceFactory)) {
    *object = self;
    return S_OK;
  }
  *object = NULL;
  return E_NOINTERFACE;
}

/* ---- ID3D12SDKConfiguration / ID3D12SDKConfiguration1 ---- */
static HRESULT WINAPI sdk_set_version(stub_object *self, UINT version, LPCSTR path) {
  char line[300];
  (void)self;
  wsprintfA(line, "proxy: ID3D12SDKConfiguration::SetSDKVersion(%u, %s) -> S_OK", version,
            path ? path : "(null)");
  log_line(line);
  return S_OK;
}

static HRESULT WINAPI sdk_create_device_factory(stub_object *self, UINT version, LPCSTR path,
                                                const GUID *riid, void **ppv);
static void WINAPI sdk_free_unused(stub_object *self) { (void)self; log_line("proxy: FreeUnusedSDKs"); }

static const void *sdk_config_vtable[] = {
  (const void *)sdk_query_interface,
  (const void *)stub_add_ref,
  (const void *)stub_release,
  (const void *)sdk_set_version,
  (const void *)sdk_create_device_factory,
  (const void *)sdk_free_unused,
};

static stub_object g_sdk_config = {sdk_config_vtable};

/* ---- ID3D12DeviceFactory (vtable order from d3d12.h, SDK 602) ---- */
static HRESULT WINAPI df_initialize(stub_object *self) { (void)self; log_line("proxy: DeviceFactory::InitializeFromGlobalState -> S_OK"); return S_OK; }
static HRESULT WINAPI df_apply(stub_object *self) { (void)self; log_line("proxy: DeviceFactory::ApplyToGlobalState -> S_OK"); return S_OK; }
static HRESULT WINAPI df_set_flags(stub_object *self, D3D12_DEVICE_FACTORY_FLAGS flags) {
  char line[80];
  (void)self;
  wsprintfA(line, "proxy: DeviceFactory::SetFlags(%d)", flags);
  log_line(line);
  return S_OK;
}
static D3D12_DEVICE_FACTORY_FLAGS WINAPI df_get_flags(stub_object *self) { (void)self; return 0; }

static HRESULT WINAPI df_get_configuration_interface(stub_object *self, const GUID *clsid,
                                                     const GUID *iid, void **ppv);
static HRESULT WINAPI df_enable_experimental(stub_object *self, UINT count, const GUID *iids,
                                             void *configs, UINT *sizes) {
  char line[120];
  (void)self;
  wsprintfA(line, "proxy: DeviceFactory::EnableExperimentalFeatures(%u)", count);
  log_line(line);
  load_real();
  if (!real_enable_experimental) return E_NOTIMPL;
  return real_enable_experimental(count, iids, configs, sizes);
}

static HRESULT WINAPI df_create_device(stub_object *self, void *adapter, UINT level,
                                       const GUID *riid, void **device) {
  HRESULT hr;
  char line[160];
  (void)self;
  load_real();
  hr = real_create_device ? real_create_device(adapter, level, riid, device) : E_NOINTERFACE;
  wsprintfA(line, "proxy: DeviceFactory::CreateDevice(adapter=%p, level=0x%x) -> 0x%08lX",
            adapter, level, (unsigned long)hr);
  log_line(line);
  return hr;
}

static const void *device_factory_vtable[] = {
  (const void *)df_query_interface,
  (const void *)stub_add_ref,
  (const void *)stub_release,
  (const void *)df_initialize,
  (const void *)df_apply,
  (const void *)df_set_flags,
  (const void *)df_get_flags,
  (const void *)df_get_configuration_interface,
  (const void *)df_enable_experimental,
  (const void *)df_create_device,
};

static stub_object g_device_factory = {device_factory_vtable};

/* forwards to this module's D3D12GetInterface */
static HRESULT WINAPI df_get_configuration_interface(stub_object *self, const GUID *clsid,
                                                     const GUID *iid, void **ppv) {
  HRESULT hr;
  char line[160];
  (void)self;
  log_guid("DeviceFactory::GetConfigurationInterface clsid", clsid);
  if (!ppv) return E_POINTER;
  *ppv = NULL;
  hr = D3D12GetInterface(clsid, iid, ppv);
  wsprintfA(line, "proxy: DeviceFactory::GetConfigurationInterface -> 0x%08lX",
            (unsigned long)hr);
  log_line(line);
  return hr;
}

static HRESULT WINAPI sdk_create_device_factory(stub_object *self, UINT version, LPCSTR path,
                                                const GUID *riid, void **ppv) {
  char line[300];
  (void)self;
  wsprintfA(line, "proxy: SDKConfiguration1::CreateDeviceFactory(%u, %s)", version,
            path ? path : "(null)");
  log_line(line);
  if (!ppv) return E_POINTER;
  if (riid && !guid_equal(riid, &IID_ID3D12DeviceFactory) && !guid_equal(riid, &IID_IUnknown)) {
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  *ppv = &g_device_factory;
  return S_OK;
}

__declspec(dllexport) HRESULT WINAPI D3D12GetInterface(const GUID *clsid, const GUID *riid,
                                                       void **object) {
  if (!object) return E_POINTER;
  *object = NULL;
  if (!clsid) return E_INVALIDARG;
  load_real();
  if (stub_only) return E_NOINTERFACE;

  if (guid_equal(clsid, &CLSID_D3D12SDKConfiguration)) {
    log_guid("D3D12GetInterface(CLSID_D3D12SDKConfiguration)", riid);
    if (!riid || guid_equal(riid, &IID_ID3D12SDKConfiguration) ||
        guid_equal(riid, &IID_ID3D12SDKConfiguration1) || guid_equal(riid, &IID_IUnknown)) {
      *object = &g_sdk_config;
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  if (guid_equal(clsid, &CLSID_D3D12DeviceFactory)) {
    log_guid("D3D12GetInterface(CLSID_D3D12DeviceFactory)", riid);
    if (!riid || guid_equal(riid, &IID_ID3D12DeviceFactory) || guid_equal(riid, &IID_IUnknown)) {
      *object = &g_device_factory;
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  if (real_get_interface) {
    return real_get_interface(clsid, riid, object);
  }
  return E_NOINTERFACE;
}

__declspec(dllexport) HRESULT WINAPI D3D12CreateDevice(void *adapter, UINT level,
                                                       const GUID *riid, void **device) {
  HRESULT hr;
  char line[160];
  load_real();
  if (stub_only) return E_NOINTERFACE;
  hr = real_create_device ? real_create_device(adapter, level, riid, device) : E_NOINTERFACE;
  if (FAILED(hr) && adapter != NULL && real_create_device) {
    hr = real_create_device(NULL, level, riid, device);
    wsprintfA(line, "proxy: retried with the default adapter -> 0x%08lX", (unsigned long)hr);
    log_line(line);
  }
  wsprintfA(line, "proxy: D3D12CreateDevice(adapter=%p, level=0x%x) -> 0x%08lX", adapter, level,
            (unsigned long)hr);
  log_line(line);
  return hr;
}

__declspec(dllexport) HRESULT WINAPI D3D12GetDebugInterface(const GUID *riid, void **debug) {
  load_real();
  if (!real_get_debug_interface) return E_NOINTERFACE;
  return real_get_debug_interface(riid, debug);
}

__declspec(dllexport) HRESULT WINAPI D3D12EnableExperimentalFeatures(UINT count, const GUID *iids,
                                                                     void *configs, UINT *sizes) {
  load_real();
  if (!real_enable_experimental) return E_NOTIMPL;
  return real_enable_experimental(count, iids, configs, sizes);
}

__declspec(dllexport) HRESULT WINAPI D3D12SerializeVersionedRootSignature(const void *desc,
                                                                          void **blob,
                                                                          void **error) {
  load_real();
  if (!real_serialize_versioned) return E_NOTIMPL;
  return real_serialize_versioned(desc, blob, error);
}

__declspec(dllexport) HRESULT WINAPI D3D12SerializeRootSignature(const void *desc, UINT version,
                                                                 void **blob, void **error) {
  load_real();
  if (!real_serialize_root) return E_NOTIMPL;
  return real_serialize_root(desc, version, blob, error);
}

__declspec(dllexport) HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void *data,
                                                                          SIZE_T data_size,
                                                                          const GUID *riid,
                                                                          void **deserializer) {
  load_real();
  if (!real_create_rs_deserializer) return E_NOTIMPL;
  return real_create_rs_deserializer(data, (UINT)data_size, riid, deserializer);
}


/* The Agility core exports this as a data export; the game reads it to learn the
 * runtime's SDK version. The shipped forwarder reported 619 (0x26B). */
__declspec(dllexport) unsigned int D3D12SDKVersion = 619;

typedef HRESULT (WINAPI *vs_deser_t)(const void *, void **, const GUID *, void **);
static vs_deser_t real_vs_deserializer;

__declspec(dllexport) HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(
    const void *data, void **deserializer, const GUID *riid, void **out) {
  load_real();
  if (!real_module) return E_NOTIMPL;
  if (!real_vs_deserializer)
    real_vs_deserializer = (vs_deser_t)GetProcAddress(real_module,
        "D3D12CreateVersionedRootSignatureDeserializer");
  if (!real_vs_deserializer) return E_NOTIMPL;
  return real_vs_deserializer(data, deserializer, riid, out);
}

__declspec(dllexport) HRESULT WINAPI D3D12CoreCreateLayeredDevice(
    const void *desc, UINT size, const GUID *riid, void **device) {
  typedef HRESULT (WINAPI *fn_t)(const void *, UINT, const GUID *, void **);
  fn_t fn;
  load_real();
  if (!real_module) return E_NOTIMPL;
  fn = (fn_t)GetProcAddress(real_module, "D3D12CoreCreateLayeredDevice");
  return fn ? fn(desc, size, riid, device) : E_NOTIMPL;
}

__declspec(dllexport) SIZE_T WINAPI D3D12CoreGetLayeredDeviceSize(const void *desc, UINT size) {
  typedef SIZE_T (WINAPI *fn_t)(const void *, UINT);
  fn_t fn;
  load_real();
  if (!real_module) return 0;
  fn = (fn_t)GetProcAddress(real_module, "D3D12CoreGetLayeredDeviceSize");
  return fn ? fn(desc, size) : 0;
}

__declspec(dllexport) HRESULT WINAPI D3D12CoreRegisterLayers(void) {
  typedef HRESULT (WINAPI *fn_t)(void);
  fn_t fn;
  load_real();
  if (!real_module) return E_NOTIMPL;
  fn = (fn_t)GetProcAddress(real_module, "D3D12CoreRegisterLayers");
  return fn ? fn() : E_NOTIMPL;
}

__declspec(dllexport) void *WINAPI GetBehaviorValue(void) { return NULL; }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    log_line("proxy: loaded next to the game (v2, full vtables)");
  }
  return TRUE;
}
