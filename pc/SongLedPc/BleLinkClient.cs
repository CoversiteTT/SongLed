using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Storage.Streams;

namespace SongLedPc;

internal sealed class BleLinkClient : IDisposable
{
    private static readonly Guid ServiceUuid = Guid.Parse("12345678-1234-5678-1234-56789abcdef0");
    private static readonly Guid CharCmdTxUuid = Guid.Parse("12345678-1234-5678-1234-56789abcdef1"); // ESP32 -> PC
    private static readonly Guid CharCmdRxUuid = Guid.Parse("12345678-1234-5678-1234-56789abcdef2"); // PC -> ESP32

    private readonly Logger _log;
    private readonly Action<string> _lineHandler;
    private readonly SemaphoreSlim _sendLock = new(1, 1);
    private readonly object _rxLock = new();
    private readonly List<byte> _rxBytes = new();

    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _cmdTxChar;
    private GattCharacteristic? _cmdRxChar;
    private string? _lastDeviceId;
    private string? _lastDeviceName;

    public bool IsConnected => _device != null && _service != null && _cmdTxChar != null && _cmdRxChar != null;
    public string ConnectedName => _device?.Name ?? "BLE";
    public string? ConnectedDeviceId => _device?.DeviceId;
    public string? LastDeviceId => _lastDeviceId;
    public string? LastDeviceName => _lastDeviceName;

    public BleLinkClient(Logger log, Action<string> lineHandler)
    {
        _log = log;
        _lineHandler = lineHandler;
    }

    public async Task<bool> ConnectAsync(string? nameHint = null, string? preferredDeviceId = null, CancellationToken cancellationToken = default)
    {
        if (IsConnected) return true;

        try
        {
            if (!string.IsNullOrWhiteSpace(preferredDeviceId))
            {
                if (await TryConnectByDeviceIdAsync(preferredDeviceId!, cancellationToken))
                {
                    return true;
                }
                _log.Info("BLE preferred device connect failed, fallback to scan");
            }
            if (await TryConnectViaServiceSelectorAsync(nameHint, cancellationToken))
            {
                return true;
            }
            if (await TryConnectViaDeviceScanAsync(nameHint, cancellationToken))
            {
                return true;
            }
            if (await TryConnectViaAdvertisementScanAsync(nameHint, cancellationToken))
            {
                return true;
            }
            return false;
        }
        catch (Exception ex)
        {
            _log.Info($"BLE connect failed: {ex.Message}");
            await DisconnectAsync();
            return false;
        }
    }

    public async Task<IReadOnlyList<(string Id, string Name)>> ListDevicesAsync(string? nameHint = null)
    {
        try
        {
            var list = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

            void AddDevice(string? id, string? name)
            {
                if (string.IsNullOrWhiteSpace(id)) return;
                string displayName = string.IsNullOrWhiteSpace(name)
                    ? $"[Unnamed] {ShortId(id)}"
                    : name!;

                if (list.TryGetValue(id!, out var oldName))
                {
                    if (oldName.StartsWith("[Unnamed]", StringComparison.OrdinalIgnoreCase) &&
                        !displayName.StartsWith("[Unnamed]", StringComparison.OrdinalIgnoreCase))
                    {
                        list[id!] = displayName;
                    }
                    return;
                }
                list[id!] = displayName;
            }

            // 1) 优先按服务 UUID 扫描（最可靠，直接匹配 SongLed 服务）
            string svcSelector = GattDeviceService.GetDeviceSelectorFromUuid(ServiceUuid);
            var services = await DeviceInformation.FindAllAsync(svcSelector);
            foreach (var svcInfo in services)
            {
                try
                {
                    var svc = await GattDeviceService.FromIdAsync(svcInfo.Id);
                    if (svc == null) continue;

                    string deviceId = svc.DeviceId;
                    string deviceName = svcInfo.Name;

                    try
                    {
                        var dev = await BluetoothLEDevice.FromIdAsync(deviceId);
                        if (dev != null && !string.IsNullOrWhiteSpace(dev.Name))
                        {
                            deviceName = dev.Name;
                        }
                        dev?.Dispose();
                    }
                    catch
                    {
                        // ignore
                    }

                    AddDevice(deviceId, deviceName);
                    svc.Dispose();
                }
                catch
                {
                    // ignore single service failure
                }
            }

            // 2) 通用 BLE 扫描兜底（用于服务发现失败场景）
            string devSelector = BluetoothLEDevice.GetDeviceSelector();
            var devices = await DeviceInformation.FindAllAsync(devSelector);
            bool strictFilter = list.Count > 0;
            foreach (var d in devices)
            {
                string name = d.Name ?? string.Empty;
                if (strictFilter)
                {
                    // 已经有服务匹配结果时，只补充可识别的 SongLed 设备
                    if (!name.Contains("SongLed", StringComparison.OrdinalIgnoreCase) &&
                        (string.IsNullOrWhiteSpace(nameHint) ||
                         !name.Contains(nameHint, StringComparison.OrdinalIgnoreCase)))
                    {
                        continue;
                    }
                }
                AddDevice(d.Id, name);
            }

            // 3) BLE 广播扫描兜底（可抓到服务枚举拿不到的设备）
            var adv = await ScanAdvertisementAsync(TimeSpan.FromSeconds(2));
            foreach (var item in adv)
            {
                string id = $"ADDR:{item.Address:X12}";
                string name = string.IsNullOrWhiteSpace(item.Name)
                    ? $"[Unnamed] {item.Address:X12}"
                    : item.Name;
                AddDevice(id, name);
            }

            return list
                .Select(kv => (kv.Key, kv.Value))
                .OrderBy(x => x.Value, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }
        catch (Exception ex)
        {
            _log.Info($"BLE list failed: {ex.Message}");
            return Array.Empty<(string, string)>();
        }
    }

    private static string ShortId(string id)
    {
        if (string.IsNullOrWhiteSpace(id)) return "unknown";
        int idx = id.LastIndexOf('#');
        if (idx >= 0 && idx + 1 < id.Length) return id[(idx + 1)..];
        idx = id.LastIndexOf('-');
        if (idx >= 0 && idx + 1 < id.Length) return id[(idx + 1)..];
        if (id.Length <= 12) return id;
        return id[^12..];
    }

    private async Task<bool> TryConnectByDeviceIdAsync(string deviceId, CancellationToken cancellationToken)
    {
        if (deviceId.StartsWith("ADDR:", StringComparison.OrdinalIgnoreCase))
        {
            if (TryParseAddress(deviceId, out ulong addr))
            {
                return await TryConnectByAddressAsync(addr, cancellationToken);
            }
            return false;
        }

        BluetoothLEDevice? device = null;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            device = await BluetoothLEDevice.FromIdAsync(deviceId);
            if (device == null)
            {
                _log.Info("BLE manual connect: device open failed");
                return false;
            }

            var services = await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            {
                _log.Info($"BLE manual connect: target service missing ({services.Status})");
                device.Dispose();
                return false;
            }

            var service = services.Services[0];
            bool ok = await TryBindServiceAsync(service, device);
            if (ok) return true;

            service.Dispose();
            device.Dispose();
            return false;
        }
        catch (Exception ex)
        {
            _log.Info($"BLE manual connect failed: {ex.Message}");
            device?.Dispose();
            return false;
        }
    }

    private async Task<bool> TryConnectViaAdvertisementScanAsync(string? nameHint, CancellationToken cancellationToken)
    {
        var adv = await ScanAdvertisementAsync(TimeSpan.FromSeconds(2), cancellationToken);
        if (adv.Count == 0)
        {
            _log.Info("BLE adv scan: no advertiser found");
            return false;
        }

        var candidates = adv;
        if (!string.IsNullOrWhiteSpace(nameHint))
        {
            candidates = adv
                .Where(x => !string.IsNullOrWhiteSpace(x.Name) &&
                            x.Name.Contains(nameHint, StringComparison.OrdinalIgnoreCase))
                .ToList();
        }
        if (candidates.Count == 0)
        {
            candidates = adv
                .Where(x => !string.IsNullOrWhiteSpace(x.Name) &&
                            x.Name.Contains("SongLed", StringComparison.OrdinalIgnoreCase))
                .ToList();
        }
        if (candidates.Count == 0) candidates = adv;

        foreach (var c in candidates.OrderByDescending(x => x.Rssi))
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (await TryConnectByAddressAsync(c.Address, cancellationToken))
            {
                return true;
            }
        }
        return false;
    }

    private async Task<bool> TryConnectByAddressAsync(ulong address, CancellationToken cancellationToken)
    {
        BluetoothLEDevice? device = null;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            device = await BluetoothLEDevice.FromBluetoothAddressAsync(address);
            if (device == null)
            {
                _log.Info($"BLE addr connect: open failed 0x{address:X}");
                return false;
            }

            var services = await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            {
                _log.Info($"BLE addr connect: target service missing ({services.Status})");
                device.Dispose();
                return false;
            }

            var service = services.Services[0];
            bool ok = await TryBindServiceAsync(service, device);
            if (ok) return true;

            service.Dispose();
            device.Dispose();
            return false;
        }
        catch (Exception ex)
        {
            _log.Info($"BLE addr connect failed: {ex.Message}");
            device?.Dispose();
            return false;
        }
    }

    private sealed class AdvHit
    {
        public ulong Address { get; set; }
        public string Name { get; set; } = string.Empty;
        public short Rssi { get; set; }
    }

    private async Task<List<AdvHit>> ScanAdvertisementAsync(TimeSpan duration, CancellationToken cancellationToken = default)
    {
        var found = new Dictionary<ulong, AdvHit>();
        var watcher = new BluetoothLEAdvertisementWatcher
        {
            ScanningMode = BluetoothLEScanningMode.Active
        };

        void OnReceived(BluetoothLEAdvertisementWatcher _, BluetoothLEAdvertisementReceivedEventArgs args)
        {
            lock (found)
            {
                if (!found.TryGetValue(args.BluetoothAddress, out var hit))
                {
                    hit = new AdvHit { Address = args.BluetoothAddress };
                    found[args.BluetoothAddress] = hit;
                }

                if (!string.IsNullOrWhiteSpace(args.Advertisement.LocalName))
                {
                    hit.Name = args.Advertisement.LocalName;
                }
                hit.Rssi = args.RawSignalStrengthInDBm;
            }
        }

        try
        {
            watcher.Received += OnReceived;
            watcher.Start();
            await Task.Delay(duration, cancellationToken);
        }
        catch (OperationCanceledException)
        {
            // ignore
        }
        catch (Exception ex)
        {
            _log.Info($"BLE adv watcher failed: {ex.Message}");
        }
        finally
        {
            try { watcher.Stop(); } catch { /* ignore */ }
            watcher.Received -= OnReceived;
        }

        lock (found)
        {
            return found.Values.ToList();
        }
    }

    private static bool TryParseAddress(string id, out ulong address)
    {
        address = 0;
        if (!id.StartsWith("ADDR:", StringComparison.OrdinalIgnoreCase)) return false;
        string hex = id.Substring(5).Trim();
        return ulong.TryParse(hex, System.Globalization.NumberStyles.HexNumber, null, out address);
    }

    private async Task<bool> TryConnectViaServiceSelectorAsync(string? nameHint, CancellationToken cancellationToken)
    {
        string selector = GattDeviceService.GetDeviceSelectorFromUuid(ServiceUuid);
        var services = await DeviceInformation.FindAllAsync(selector);
        if (services.Count == 0)
        {
            _log.Info("BLE scan(service): no SongLed service found");
            return false;
        }

        DeviceInformation? target = null;
        if (!string.IsNullOrWhiteSpace(nameHint))
        {
            target = services.FirstOrDefault(s =>
                s.Name.Contains(nameHint, StringComparison.OrdinalIgnoreCase));
        }
        target ??= services.FirstOrDefault(s =>
            s.Name.Contains("SongLed", StringComparison.OrdinalIgnoreCase));
        target ??= services[0];
        if (target == null) return false;

        cancellationToken.ThrowIfCancellationRequested();

        GattDeviceService? service = await GattDeviceService.FromIdAsync(target.Id);
        if (service == null)
        {
            _log.Info("BLE connect(service): failed to open GATT service");
            return false;
        }

        bool ok = await TryBindServiceAsync(service, null);
        if (!ok)
        {
            service.Dispose();
        }
        return ok;
    }

    private async Task<bool> TryConnectViaDeviceScanAsync(string? nameHint, CancellationToken cancellationToken)
    {
        string selector = BluetoothLEDevice.GetDeviceSelector();
        var devices = await DeviceInformation.FindAllAsync(selector);
        if (devices.Count == 0)
        {
            _log.Info("BLE scan(device): no BLE devices found");
            return false;
        }

        var namedCandidates = devices
            .Where(d =>
                (!string.IsNullOrWhiteSpace(d.Name) &&
                 (d.Name.Contains("SongLed", StringComparison.OrdinalIgnoreCase) ||
                  (!string.IsNullOrWhiteSpace(nameHint) &&
                   d.Name.Contains(nameHint, StringComparison.OrdinalIgnoreCase)))))
            .ToList();

        // Fallback: include paired/known BLE devices, so users don't need manual select every launch.
        var fallbackCandidates = devices
            .Where(d => !namedCandidates.Any(n => string.Equals(n.Id, d.Id, StringComparison.OrdinalIgnoreCase)))
            .Where(d => d.Pairing?.IsPaired == true || !string.IsNullOrWhiteSpace(d.Name))
            .Take(16)
            .ToList();

        var candidates = namedCandidates.Concat(fallbackCandidates).ToList();
        if (candidates.Count == 0)
        {
            _log.Info("BLE scan(device): no candidate devices");
            return false;
        }

        foreach (var info in candidates)
        {
            cancellationToken.ThrowIfCancellationRequested();
            BluetoothLEDevice? device = null;
            try
            {
                device = await BluetoothLEDevice.FromIdAsync(info.Id);
                if (device == null) continue;
                var services = await device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
                if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
                {
                    _log.Info($"BLE scan(device): {device.Name} has no target service ({services.Status})");
                    device.Dispose();
                    continue;
                }

                var service = services.Services[0];
                bool ok = await TryBindServiceAsync(service, device);
                if (ok) return true;

                service.Dispose();
                device.Dispose();
            }
            catch (Exception ex)
            {
                _log.Info($"BLE scan(device): {info.Name} failed: {ex.Message}");
                device?.Dispose();
            }
        }
        return false;
    }

    private async Task<bool> TryBindServiceAsync(
        GattDeviceService service,
        BluetoothLEDevice? existingDevice)
    {
        try
        {
            var access = await service.RequestAccessAsync();
            if (access != DeviceAccessStatus.Allowed)
            {
                _log.Info($"BLE bind: access denied ({access})");
                return false;
            }

            var charsResult = await service.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
            if (charsResult.Status != GattCommunicationStatus.Success)
            {
                _log.Info($"BLE bind: characteristic discovery failed ({charsResult.Status})");
                return false;
            }

            var cmdTx = charsResult.Characteristics.FirstOrDefault(c => c.Uuid == CharCmdTxUuid);
            var cmdRx = charsResult.Characteristics.FirstOrDefault(c => c.Uuid == CharCmdRxUuid);
            if (cmdTx == null || cmdRx == null)
            {
                _log.Info("BLE bind: required characteristics missing");
                return false;
            }

            var notifyStatus = await cmdTx.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue.Notify);
            if (notifyStatus != GattCommunicationStatus.Success)
            {
                _log.Info($"BLE bind: enable notify failed ({notifyStatus})");
                return false;
            }

            await DisconnectAsync();

            cmdTx.ValueChanged += OnCmdTxValueChanged;
            _cmdTxChar = cmdTx;
            _cmdRxChar = cmdRx;
            _service = service;
            _device = existingDevice ?? await BluetoothLEDevice.FromIdAsync(service.DeviceId);
            if (_device != null)
            {
                _device.ConnectionStatusChanged += OnConnectionStatusChanged;
                _lastDeviceId = _device.DeviceId;
                _lastDeviceName = _device.Name;
            }
            else
            {
                _lastDeviceId = service.DeviceId;
            }
            _log.Info($"BLE connected: {ConnectedName}");
            return true;
        }
        catch (Exception ex)
        {
            _log.Info($"BLE bind failed: {ex.Message}");
            return false;
        }
    }

    public async Task DisconnectAsync()
    {
        try
        {
            if (_cmdTxChar != null)
            {
                try
                {
                    // Don't block shutdown/reconnect on CCCD write; timeout fast.
                    var cccdTask = _cmdTxChar
                        .WriteClientCharacteristicConfigurationDescriptorAsync(
                            GattClientCharacteristicConfigurationDescriptorValue.None)
                        .AsTask();
                    await Task.WhenAny(cccdTask, Task.Delay(300));
                }
                catch
                {
                    // ignore
                }
                _cmdTxChar.ValueChanged -= OnCmdTxValueChanged;
            }
        }
        catch
        {
            // ignore
        }

        _cmdTxChar = null;
        _cmdRxChar = null;

        _service?.Dispose();
        _service = null;

        if (_device != null)
        {
            _device.ConnectionStatusChanged -= OnConnectionStatusChanged;
        }
        _device?.Dispose();
        _device = null;

        lock (_rxLock)
        {
            _rxBytes.Clear();
        }
    }

    public async Task<bool> SendLineAsync(string line)
    {
        if (!IsConnected || _cmdRxChar == null) return false;
        byte[] bytes = Encoding.UTF8.GetBytes(line + "\n");

        await _sendLock.WaitAsync();
        try
        {
            var writer = new DataWriter();
            writer.WriteBytes(bytes);
            var status = await _cmdRxChar.WriteValueAsync(
                writer.DetachBuffer(),
                GattWriteOption.WriteWithoutResponse);
            return status == GattCommunicationStatus.Success;
        }
        catch (Exception ex)
        {
            _log.Info($"BLE send failed: {ex.Message}");
            return false;
        }
        finally
        {
            _sendLock.Release();
        }
    }

    private void OnCmdTxValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        try
        {
            var reader = DataReader.FromBuffer(args.CharacteristicValue);
            var bytes = new byte[reader.UnconsumedBufferLength];
            reader.ReadBytes(bytes);

            lock (_rxLock)
            {
                _rxBytes.AddRange(bytes);
                while (true)
                {
                    int newline = _rxBytes.FindIndex(b => b == (byte)'\n' || b == (byte)'\r');
                    if (newline < 0) break;

                    var lineBytes = _rxBytes.Take(newline).ToArray();
                    int removeCount = 1;
                    while (newline + removeCount < _rxBytes.Count &&
                           (_rxBytes[newline + removeCount] == (byte)'\n' ||
                            _rxBytes[newline + removeCount] == (byte)'\r'))
                    {
                        removeCount++;
                    }
                    _rxBytes.RemoveRange(0, newline + removeCount);

                    string line = Encoding.UTF8.GetString(lineBytes).Trim();
                    if (line.Length > 0)
                    {
                        _lineHandler(line);
                    }
                }
            }
        }
        catch (Exception ex)
        {
            _log.Info($"BLE rx parse failed: {ex.Message}");
        }
    }

    private void OnConnectionStatusChanged(BluetoothLEDevice sender, object args)
    {
        if (sender.ConnectionStatus == BluetoothConnectionStatus.Connected) return;
        _log.Info("BLE disconnected");
        _ = Task.Run(async () =>
        {
            try
            {
                await DisconnectAsync();
            }
            catch
            {
                // ignore
            }
        });
    }

    public void Dispose()
    {
        try
        {
            DisconnectAsync().GetAwaiter().GetResult();
        }
        catch
        {
            // ignore
        }
        _sendLock.Dispose();
    }
}
