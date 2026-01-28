#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <shlwapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <initguid.h>
#include <devguid.h>
#include <comdef.h>

#ifndef SPDRP_PORTNAME
#define SPDRP_PORTNAME (0x0000000F)
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT TIMER_CONNECT = 1;
constexpr UINT WM_SERIAL = WM_APP + 2;

constexpr wchar_t kAppName[] = L"SongLedPcCpp";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"SongLedPc";
constexpr wchar_t kVCRuntimeUrl[] = L"https://aka.ms/vs/17/release/vc_redist.x64.exe";

bool CheckRuntimeDll(const wchar_t *name) {
  HMODULE lib = LoadLibraryW(name);
  if (lib) {
    FreeLibrary(lib);
    return true;
  }
  return false;
}

bool EnsureVCRuntime() {
  const wchar_t *required[] = {L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll"};
  for (const auto *dll : required) {
    if (!CheckRuntimeDll(dll)) {
      int choice = MessageBoxW(
          nullptr,
          L"Microsoft Visual C++ Runtime is missing.\nOpen download page?",
          L"SongLed PC",
          MB_ICONERROR | MB_OKCANCEL | MB_SETFOREGROUND);
      if (choice == IDOK) {
        ShellExecuteW(nullptr, L"open", kVCRuntimeUrl, nullptr, nullptr, SW_SHOWNORMAL);
      }
      return false;
    }
  }
  return true;
}

std::wstring Utf8ToWide(const std::string &s) {
  if (s.empty()) return L"";
  int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (len <= 0) return L"";
  std::wstring out(len - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
  return out;
}

std::string WideToUtf8(const std::wstring &s) {
  if (s.empty()) return std::string();
  int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0) return std::string();
  std::string out(len - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), len, nullptr, nullptr);
  return out;
}

std::wstring SanitizeDeviceName(const std::wstring &name) {
  std::wstring out = name;
  for (auto &ch : out) {
    if (ch == L'\r' || ch == L'\n') ch = L' ';
  }
  return out;
}

std::vector<std::wstring> EnumerateComPorts(std::wstring *matchVidPid) {
  std::vector<std::wstring> result;
  HDEVINFO info = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
  if (info == INVALID_HANDLE_VALUE) return result;

  DWORD index = 0;
  SP_DEVINFO_DATA dev = {};
  dev.cbSize = sizeof(dev);
  while (SetupDiEnumDeviceInfo(info, index++, &dev)) {
    wchar_t portName[256] = {};
    DWORD regType = 0;
    DWORD size = 0;
    if (SetupDiGetDeviceRegistryPropertyW(info, &dev, SPDRP_PORTNAME, &regType,
                                          reinterpret_cast<PBYTE>(portName),
                                          sizeof(portName), &size)) {
      if (wcsncmp(portName, L"COM", 3) == 0) {
        result.emplace_back(portName);
      }
    }
  }

  if (matchVidPid && !matchVidPid->empty()) {
    std::vector<std::wstring> filtered;
    index = 0;
    dev = {};
    dev.cbSize = sizeof(dev);
    while (SetupDiEnumDeviceInfo(info, index++, &dev)) {
      wchar_t hardwareId[1024] = {};
      DWORD regType = 0;
      DWORD size = 0;
      if (!SetupDiGetDeviceRegistryPropertyW(info, &dev, SPDRP_HARDWAREID, &regType,
                                             reinterpret_cast<PBYTE>(hardwareId),
                                             sizeof(hardwareId), &size)) {
        continue;
      }
      std::wstring hw = hardwareId;
      if (StrStrIW(hw.c_str(), matchVidPid->c_str()) == nullptr) {
        continue;
      }
      wchar_t portName[256] = {};
      if (SetupDiGetDeviceRegistryPropertyW(info, &dev, SPDRP_PORTNAME, &regType,
                                            reinterpret_cast<PBYTE>(portName),
                                            sizeof(portName), &size)) {
        if (wcsncmp(portName, L"COM", 3) == 0) {
          filtered.emplace_back(portName);
        }
      }
    }
    SetupDiDestroyDeviceInfoList(info);
    return filtered;
  }

  SetupDiDestroyDeviceInfoList(info);
  return result;
}

bool IsAutoStartEnabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
  wchar_t value[1024] = {};
  DWORD size = sizeof(value);
  DWORD type = 0;
  bool enabled = (RegQueryValueExW(key, kRunValue, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS);
  RegCloseKey(key);
  return enabled;
}

void SetAutoStartEnabled(bool enable) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &key) != ERROR_SUCCESS) return;
  if (enable) {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE *>(path),
                   static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, kRunValue);
  }
  RegCloseKey(key);
}

class SerialPort {
public:
  using LineHandler = void(*)(const std::string &line, void *ctx);

  SerialPort() = default;
  ~SerialPort() { Close(); }

  bool Open(const std::wstring &port, LineHandler handler, void *ctx) {
    Close();
    std::wstring path = L"\\\\.\\" + port;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      lastError_ = GetLastError();
      return false;
    }
    SetupComm(h, 4096, 4096);

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
      lastError_ = GetLastError();
      CloseHandle(h);
      return false;
    }
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(h, &dcb)) {
      lastError_ = GetLastError();
      CloseHandle(h);
      return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    if (!SetCommTimeouts(h, &timeouts)) {
      lastError_ = GetLastError();
      CloseHandle(h);
      return false;
    }

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    handle_ = h;
    lastError_ = 0;
    handler_ = handler;
    ctx_ = ctx;
    running_ = true;
    reader_ = std::thread([this]() { ReadLoop(); });
    return true;
  }

  void Close() {
    running_ = false;
    if (reader_.joinable()) reader_.join();
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
  }

  bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
  DWORD LastError() const { return lastError_; }

  void WriteLine(const std::string &line) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (handle_ == INVALID_HANDLE_VALUE) return;
    std::string out = line + "\n";
    DWORD written = 0;
    WriteFile(handle_, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
  }

private:
  void ReadLoop() {
    std::string buf;
    char temp[128];
    while (running_) {
      DWORD read = 0;
      if (!ReadFile(handle_, temp, sizeof(temp), &read, nullptr)) {
        break;
      }
      if (read == 0) continue;
      buf.append(temp, temp + read);
      size_t pos;
      while ((pos = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, pos);
        buf.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (handler_) handler_(line, ctx_);
      }
    }
  }

  HANDLE handle_ = INVALID_HANDLE_VALUE;
  std::thread reader_;
  std::atomic<bool> running_{false};
  std::mutex writeMutex_;
  LineHandler handler_ = nullptr;
  void *ctx_ = nullptr;
  DWORD lastError_ = 0;
};

struct AudioDevice {
  std::wstring id;
  std::wstring name;
};

class AudioManager {
public:
  AudioManager() {
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                     reinterpret_cast<void **>(&enumerator_));
  }

  ~AudioManager() {
    if (enumerator_) enumerator_->Release();
  }

  void RefreshDevices() {
    devices_.clear();
    devicesValid_ = false;
    if (!enumerator_) return;
    IMMDeviceCollection *collection = nullptr;
    if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) return;
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      IMMDevice *device = nullptr;
      if (FAILED(collection->Item(i, &device))) continue;
      LPWSTR id = nullptr;
      if (FAILED(device->GetId(&id))) { device->Release(); continue; }
      IPropertyStore *store = nullptr;
      std::wstring name;
      if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store))) {
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &var))) {
          if (var.vt == VT_LPWSTR && var.pwszVal) name = var.pwszVal;
        }
        PropVariantClear(&var);
        store->Release();
      }
      AudioDevice dev;
      dev.id = id;
      dev.name = SanitizeDeviceName(name);
      devices_.push_back(dev);
      CoTaskMemFree(id);
      device->Release();
    }
    collection->Release();
    devicesValid_ = true;
  }

  void EnsureDevicesLoaded() {
    if (!devicesValid_) RefreshDevices();
  }

  bool GetDefaultDevice(IMMDevice **device) {
    if (!enumerator_) return false;
    return SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, device));
  }

  int GetVolume() {
    IMMDevice *device = nullptr;
    if (!GetDefaultDevice(&device)) return 0;
    IAudioEndpointVolume *vol = nullptr;
    int value = 0;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&vol))) {
      float scalar = 0.0f;
      if (SUCCEEDED(vol->GetMasterVolumeLevelScalar(&scalar))) {
        value = static_cast<int>(std::round(scalar * 100.0f));
      }
      vol->Release();
    }
    device->Release();
    return value;
  }

  bool GetMute() {
    IMMDevice *device = nullptr;
    if (!GetDefaultDevice(&device)) return false;
    IAudioEndpointVolume *vol = nullptr;
    bool mute = false;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&vol))) {
      BOOL m = FALSE;
      if (SUCCEEDED(vol->GetMute(&m))) mute = m != FALSE;
      vol->Release();
    }
    device->Release();
    return mute;
  }

  void SetVolume(int value) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    IMMDevice *device = nullptr;
    if (!GetDefaultDevice(&device)) return;
    IAudioEndpointVolume *vol = nullptr;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&vol))) {
      vol->SetMasterVolumeLevelScalar(value / 100.0f, nullptr);
      vol->Release();
    }
    device->Release();
  }

  void ToggleMute() {
    IMMDevice *device = nullptr;
    if (!GetDefaultDevice(&device)) return;
    IAudioEndpointVolume *vol = nullptr;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&vol))) {
      BOOL m = FALSE;
      vol->GetMute(&m);
      vol->SetMute(!m, nullptr);
      vol->Release();
    }
    device->Release();
  }

  const std::vector<AudioDevice> &Devices() const { return devices_; }

  std::wstring GetCurrentDeviceId() {
    IMMDevice *device = nullptr;
    if (!GetDefaultDevice(&device)) return L"";
    LPWSTR id = nullptr;
    std::wstring result;
    if (SUCCEEDED(device->GetId(&id))) {
      result = id;
      CoTaskMemFree(id);
    }
    device->Release();
    return result;
  }

private:
  IMMDeviceEnumerator *enumerator_ = nullptr;
  std::vector<AudioDevice> devices_;
  bool devicesValid_ = false;
};

// IPolicyConfig (undocumented) for setting default device.
// This matches the C# version used in the existing app.
struct __declspec(uuid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")) PolicyConfigClient;

struct __declspec(uuid("F8679F50-850A-41CF-9C72-430F290290C8")) IPolicyConfig : public IUnknown {
  virtual HRESULT GetMixFormat(PCWSTR, void **) = 0;
  virtual HRESULT GetDeviceFormat(PCWSTR, BOOL, void **) = 0;
  virtual HRESULT ResetDeviceFormat(PCWSTR) = 0;
  virtual HRESULT SetDeviceFormat(PCWSTR, void *, void *) = 0;
  virtual HRESULT GetProcessingPeriod(PCWSTR, BOOL, void *, void *) = 0;
  virtual HRESULT SetProcessingPeriod(PCWSTR, void *, void *) = 0;
  virtual HRESULT GetShareMode(PCWSTR, void *) = 0;
  virtual HRESULT SetShareMode(PCWSTR, void *) = 0;
  virtual HRESULT GetPropertyValue(PCWSTR, const PROPERTYKEY *, PROPVARIANT *) = 0;
  virtual HRESULT SetPropertyValue(PCWSTR, const PROPERTYKEY *, PROPVARIANT *) = 0;
  virtual HRESULT SetDefaultEndpoint(PCWSTR wszDeviceId, ERole role) = 0;
};

class App;

struct SerialContext {
  App *app = nullptr;
};

class App {
public:
  App() = default;

  bool Init(HINSTANCE inst, const std::wstring &port, const std::wstring &vidPid, bool autoStart) {
    inst_ = inst;
    portArg_ = port;
    vidPid_ = vidPid;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProcStatic;
    wc.hInstance = inst_;
    wc.lpszClassName = kAppName;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowW(kAppName, kAppName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, inst_, this);
    if (!hwnd_) return false;

    trayMenu_ = CreatePopupMenu();
    AppendMenuW(trayMenu_, MF_STRING | MF_GRAYED, 1, L"Status: Disconnected");
    AppendMenuW(trayMenu_, MF_STRING | MF_GRAYED, 2, L"Port: -");
    AppendMenuW(trayMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(trayMenu_, MF_STRING, 3, L"AutoStart: Off");
    AppendMenuW(trayMenu_, MF_STRING, 4, L"Reconnect");
    AppendMenuW(trayMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(trayMenu_, MF_STRING, 5, L"Exit");

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"SongLed PC");
    Shell_NotifyIconW(NIM_ADD, &nid);

    SetTimer(hwnd_, TIMER_CONNECT, 3000, nullptr);

    if (autoStart) {
      SetAutoStartEnabled(true);
    }

    StartConnect(true);
    return true;
  }

  void Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  void Shutdown() {
    serial_.Close();
    if (trayMenu_) DestroyMenu(trayMenu_);
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
  }

private:
  static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    App *self = nullptr;
    if (msg == WM_NCCREATE) {
      auto *cs = reinterpret_cast<CREATESTRUCTW *>(lparam);
      self = reinterpret_cast<App *>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
      self = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_TRAY:
        if (lparam == WM_RBUTTONUP) {
          POINT pt;
          GetCursorPos(&pt);
          UpdateMenu();
          SetForegroundWindow(hwnd);
          TrackPopupMenu(trayMenu_, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        }
        return 0;
      case WM_COMMAND:
        if (LOWORD(wparam) == 3) {
          SetAutoStartEnabled(!IsAutoStartEnabled());
        } else if (LOWORD(wparam) == 4) {
          StartConnect(true);
        } else if (LOWORD(wparam) == 5) {
          PostQuitMessage(0);
        }
        return 0;
      case WM_TIMER:
        if (wparam == TIMER_CONNECT) {
          StartConnect(false);
        }
        return 0;
      case WM_SERIAL:
        DrainSerialQueue();
        return 0;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  void EnqueueSerial(const std::string &line) {
    {
      std::lock_guard<std::mutex> lock(serialQueueMutex_);
      serialQueue_.push(line);
    }
    if (hwnd_) {
      PostMessageW(hwnd_, WM_SERIAL, 0, 0);
    }
  }

  void DrainSerialQueue() {
    std::queue<std::string> local;
    {
      std::lock_guard<std::mutex> lock(serialQueueMutex_);
      std::swap(local, serialQueue_);
    }
    while (!local.empty()) {
      HandleSerial(local.front());
      local.pop();
    }
  }

  void UpdateMenu() {
    std::wstring status = serial_.IsOpen() ? L"Status: Connected" : L"Status: Waiting";
    if (!serial_.IsOpen() && !lastOpenErrorText_.empty()) {
      status += L" (";
      status += lastOpenErrorText_;
      status += L")";
    }
    std::wstring port = L"Port: ";
    port += currentPort_.empty() ? L"-" : currentPort_;
    std::wstring autostart = IsAutoStartEnabled() ? L"AutoStart: On" : L"AutoStart: Off";
    ModifyMenuW(trayMenu_, 1, MF_BYCOMMAND | MF_STRING | MF_GRAYED, 1, status.c_str());
    ModifyMenuW(trayMenu_, 2, MF_BYCOMMAND | MF_STRING | MF_GRAYED, 2, port.c_str());
    ModifyMenuW(trayMenu_, 3, MF_BYCOMMAND | MF_STRING, 3, autostart.c_str());
  }

  void StartConnect(bool force) {
    if (connecting_) return;
    connecting_ = true;
    std::thread([this, force]() {
      AttemptConnect(force);
      connecting_ = false;
    }).detach();
  }

  void AttemptConnect(bool force) {
    std::wstring port = ResolvePort(force);
    if (port.empty()) {
      if (serial_.IsOpen()) serial_.Close();
      currentPort_.clear();
      return;
    }

    if (!force && serial_.IsOpen() && _wcsicmp(currentPort_.c_str(), port.c_str()) == 0) {
      return;
    }

    SerialContext *ctx = &serialCtx_;
    ctx->app = this;
    if (serial_.Open(port, &App::HandleSerialStatic, ctx)) {
      currentPort_ = port;
      serial_.WriteLine("HELLO");
      lastOpenErrorCode_ = 0;
      lastOpenErrorText_.clear();
    } else {
      DWORD code = serial_.LastError();
      if (code != 0) {
        if (code != lastOpenErrorCode_ || (GetTickCount64() - lastOpenErrorMs_ > 5000)) {
          lastOpenErrorMs_ = GetTickCount64();
          lastOpenErrorCode_ = code;
          wchar_t *msg = nullptr;
          FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                         nullptr, code, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
          if (msg) {
            lastOpenErrorText_ = msg;
            LocalFree(msg);
          } else {
            lastOpenErrorText_.clear();
          }
          if (!portArg_.empty()) {
            std::wstring text = L"Failed to open ";
            text += port;
            text += L".\n";
            if (!lastOpenErrorText_.empty()) {
              text += lastOpenErrorText_;
            } else {
              text += L"Error code: " + std::to_wstring(code);
            }
            MessageBoxW(nullptr, text.c_str(), L"SongLed PC", MB_ICONERROR | MB_OK);
          }
        }
      }
    }
  }

  std::wstring ResolvePort(bool force) {
    if (!portArg_.empty()) return portArg_;

    if (!vidPid_.empty()) {
      auto ports = EnumerateComPorts(&vidPid_);
      if (!ports.empty()) return ports.front();
    }

    auto now = GetTickCount64();
    if (force || now - lastHandshakeMs_ > 10000) {
      lastHandshakeMs_ = now;
      auto ports = EnumerateComPorts(nullptr);
      for (const auto &p : ports) {
        if (TryHandshake(p)) return p;
      }
    }

    auto ports = EnumerateComPorts(nullptr);
    if (ports.size() == 1) return ports.front();
    return L"";
  }

  bool TryHandshake(const std::wstring &port) {
    SerialPort probe;
    SerialContext ctx;
    bool ok = false;
    auto handler = [](const std::string &line, void *ctxPtr) {
      auto *pair = reinterpret_cast<std::pair<bool *, SerialPort *> *>(ctxPtr);
      if (line.rfind("HELLO", 0) == 0) {
        *pair->first = true;
      }
    };
    std::pair<bool *, SerialPort *> state{&ok, &probe};
    if (!probe.Open(port, handler, &state)) return false;
    probe.WriteLine("HELLO");
    auto start = GetTickCount64();
    while (!ok && GetTickCount64() - start < 1200) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    probe.Close();
    return ok;
  }

  static void HandleSerialStatic(const std::string &line, void *ctx) {
    auto *serialCtx = reinterpret_cast<SerialContext *>(ctx);
    if (serialCtx && serialCtx->app) serialCtx->app->EnqueueSerial(line);
  }

  void HandleSerial(const std::string &line) {
    if (line.rfind("HELLO", 0) == 0) {
      serial_.WriteLine("HELLO OK");
      SendVolumeState();
      SendSpeakerList();
      return;
    }
    if (line == "VOL GET") {
      SendVolumeState();
      return;
    }
    if (line.rfind("VOL SET", 0) == 0) {
      int value = std::atoi(line.substr(7).c_str());
      audio_.SetVolume(value);
      SendVolumeState();
      return;
    }
    if (line == "MUTE") {
      audio_.ToggleMute();
      SendVolumeState();
      return;
    }
    if (line == "SPK LIST") {
      SendSpeakerList();
      return;
    }
    if (line.rfind("SPK SET", 0) == 0) {
      int index = std::atoi(line.substr(7).c_str());
      SetDefaultSpeaker(index);
      SendCurrentSpeaker();
      return;
    }
  }

  void SendVolumeState() {
    int vol = audio_.GetVolume();
    int mute = audio_.GetMute() ? 1 : 0;
    serial_.WriteLine("VOL " + std::to_string(vol));
    serial_.WriteLine("MUTE " + std::to_string(mute));
  }

  void SendSpeakerList() {
    audio_.RefreshDevices();
    serial_.WriteLine("SPK BEGIN");
    const auto &devs = audio_.Devices();
    for (size_t i = 0; i < devs.size(); ++i) {
      std::string name = WideToUtf8(devs[i].name);
      serial_.WriteLine("SPK ITEM " + std::to_string(i) + " " + name);
    }
    serial_.WriteLine("SPK END");
    SendCurrentSpeaker();
  }

  void SendCurrentSpeaker() {
    std::wstring currentId = audio_.GetCurrentDeviceId();
    if (currentId.empty()) return;
    
    audio_.EnsureDevicesLoaded();
    const auto &devs = audio_.Devices();
    int index = -1;
    for (size_t i = 0; i < devs.size(); ++i) {
      if (devs[i].id == currentId) { index = static_cast<int>(i); break; }
    }
    if (index >= 0) {
      serial_.WriteLine("SPK CUR " + std::to_string(index));
    }
  }

  void SetDefaultSpeaker(int index) {
    audio_.EnsureDevicesLoaded();
    const auto &devs = audio_.Devices();
    if (index < 0 || index >= static_cast<int>(devs.size())) return;
    const std::wstring &id = devs[index].id;
    IPolicyConfig *policy = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(PolicyConfigClient), nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), (void **)&policy))) {
      policy->SetDefaultEndpoint(id.c_str(), eConsole);
      policy->SetDefaultEndpoint(id.c_str(), eMultimedia);
      policy->SetDefaultEndpoint(id.c_str(), eCommunications);
      policy->Release();
    }
  }

private:
  HINSTANCE inst_ = nullptr;
  HWND hwnd_ = nullptr;
  HMENU trayMenu_ = nullptr;
  std::wstring currentPort_;
  std::wstring portArg_;
  std::wstring vidPid_;
  std::atomic<bool> connecting_{false};
  ULONGLONG lastHandshakeMs_ = 0;
  ULONGLONG lastOpenErrorMs_ = 0;
  DWORD lastOpenErrorCode_ = 0;
  std::wstring lastOpenErrorText_;
  std::mutex serialQueueMutex_;
  std::queue<std::string> serialQueue_;

  SerialPort serial_;
  SerialContext serialCtx_;
  AudioManager audio_;
};

struct Args {
  std::wstring port;
  std::wstring vidPid;
  bool autoStart = false;
};

Args ParseArgs() {
  Args args;
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    std::wstring cur = argv[i];
    if (cur == L"--port" && i + 1 < argc) {
      args.port = argv[++i];
    } else if (cur == L"--vid" && i + 1 < argc) {
      std::wstring vid = argv[++i];
      if (vid.size() == 4) {
        if (!args.vidPid.empty()) args.vidPid += L"&";
        args.vidPid += L"VID_" + vid;
      }
    } else if (cur == L"--pid" && i + 1 < argc) {
      std::wstring pid = argv[++i];
      if (pid.size() == 4) {
        if (!args.vidPid.empty()) args.vidPid += L"&";
        args.vidPid += L"PID_" + pid;
      }
    } else if (cur == L"--autostart" && i + 1 < argc) {
      std::wstring val = argv[++i];
      if (val == L"on") args.autoStart = true;
      if (val == L"off") args.autoStart = false;
      if (val == L"toggle") args.autoStart = !IsAutoStartEnabled();
    }
  }
  if (argv) LocalFree(argv);
  return args;
}
}  // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
  if (!EnsureVCRuntime()) {
    return 2;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  Args args = ParseArgs();
  App app;
  if (!app.Init(hInstance, args.port, args.vidPid, args.autoStart)) {
    return 1;
  }
  app.Run();
  app.Shutdown();

  CoUninitialize();
  return 0;
}

