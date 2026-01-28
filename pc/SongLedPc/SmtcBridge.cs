using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Text;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;
using System.Threading.Tasks;
using Windows.Media.Control;

namespace SongLedPc;

internal sealed class SmtcBridge : IDisposable
{
    private readonly Logger _log;
    private readonly SerialWorker _serial;
    private readonly HttpClient _http;
    private readonly object _lyricLock = new();
    private GlobalSystemMediaTransportControlsSessionManager? _manager;
    private GlobalSystemMediaTransportControlsSession? _session;
    private string? _lastTrackKey;
    private string? _currentNcmId;
    private List<LyricEntry> _lyrics = new();
    private long _lastPositionMs = -1;
    private int _lastLyricIndex = -2;
    private DateTime _lastSessionLog = DateTime.MinValue;
    private DateTime _lastProgSent = DateTime.MinValue;
    private DateTime _lastMetaSent = DateTime.MinValue;
    private string _lastMetaKey = string.Empty;
    private long _lastProgPos = -1;
    private long _lastProgDur = -1;
    private readonly SemaphoreSlim _coverLock = new(1, 1);
    private int _coverSending;
    private CancellationTokenSource? _lyricCts;
    private bool _disposed;
    private string _lastTitle = string.Empty;
    private string _lastArtist = string.Empty;
    private ushort[]? _lastCover;
    private readonly System.Threading.Timer _timelineTimer;
    private int _pollingTimeline;
    private DateTime _lastTimelineWarn = DateTime.MinValue;

    public SmtcBridge(Logger log, SerialWorker serial)
    {
        _log = log;
        _serial = serial;
        _http = new HttpClient
        {
            Timeout = TimeSpan.FromSeconds(6)
        };
        _http.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        _http.DefaultRequestHeaders.Referrer = new Uri("https://music.163.com/");
        _timelineTimer = new System.Threading.Timer(_ => PollTimeline(), null, Timeout.Infinite, Timeout.Infinite);
    }

    public async Task InitializeAsync()
    {
        try
        {
            _manager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
            _manager.SessionsChanged += OnSessionsChanged;
            _manager.CurrentSessionChanged += OnCurrentSessionChanged;
            await RefreshSessionAsync();
            StartTimelineTimer();
        }
        catch (Exception ex)
        {
            _log.Info($"SMTC init failed: {ex.Message}");
        }
    }

    private void StartTimelineTimer()
    {
        _timelineTimer.Change(0, 100);
    }

    private void StopTimelineTimer()
    {
        _timelineTimer.Change(Timeout.Infinite, Timeout.Infinite);
    }

    private void PollTimeline()
    {
        if (_disposed || _session == null) return;
        if (Interlocked.Exchange(ref _pollingTimeline, 1) == 1) return;
        try
        {
            _ = UpdateTimelineAsync(_session);
        }
        finally
        {
            Interlocked.Exchange(ref _pollingTimeline, 0);
        }
    }

    private async void OnSessionsChanged(GlobalSystemMediaTransportControlsSessionManager sender, SessionsChangedEventArgs args)
    {
        await RefreshSessionAsync();
    }

    private async void OnCurrentSessionChanged(GlobalSystemMediaTransportControlsSessionManager sender, CurrentSessionChangedEventArgs args)
    {
        await RefreshSessionAsync();
    }

    private async Task RefreshSessionAsync()
    {
        if (_manager == null) return;
        var sessions = _manager.GetSessions();
        var target = await FindNcmSessionAsync(sessions);
        if (target == null)
        {
            await LogSessionsAsync(sessions);
        }
        if (!ReferenceEquals(_session, target))
        {
            AttachSession(target);
        }
    }

    private async Task<GlobalSystemMediaTransportControlsSession?> FindNcmSessionAsync(IReadOnlyList<GlobalSystemMediaTransportControlsSession> sessions)
    {
        foreach (var session in sessions)
        {
            try
            {
                var props = await session.TryGetMediaPropertiesAsync();
                if (props == null) continue;
                if (HasNcmId(props.Genres))
                {
                    return session;
                }
            }
            catch
            {
                // ignore and continue
            }
        }
        if (sessions.Count > 0)
        {
            _log.Info("SMTC: fallback to first session (no NCM id found)");
            return sessions[0];
        }

        return null;
    }

    private async Task LogSessionsAsync(IReadOnlyList<GlobalSystemMediaTransportControlsSession> sessions)
    {
        if (DateTime.UtcNow - _lastSessionLog < TimeSpan.FromSeconds(10)) return;
        _lastSessionLog = DateTime.UtcNow;

        foreach (var session in sessions)
        {
            try
            {
                var props = await session.TryGetMediaPropertiesAsync();
                if (props == null) continue;
                string title = props.Title ?? string.Empty;
                string artist = props.Artist ?? string.Empty;
                string genres = string.Join(",", props.Genres);
                _log.Info($"SMTC session: {session.SourceAppUserModelId} | {title} - {artist} | {genres}");
            }
            catch
            {
                // ignore
            }
        }
    }

    private static bool HasNcmId(IReadOnlyList<string> genres)
    {
        foreach (var g in genres)
        {
            if (g.StartsWith("NCM-", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    private static string? ExtractNcmId(IReadOnlyList<string> genres)
    {
        foreach (var g in genres)
        {
            if (g.StartsWith("NCM-", StringComparison.OrdinalIgnoreCase))
            {
                return g.Substring(4);
            }
        }
        return null;
    }

    private void AttachSession(GlobalSystemMediaTransportControlsSession? session)
    {
        DetachSession();
        _session = session;
        if (_session == null)
        {
            _log.Info("SMTC: no NCM session");
            return;
        }

        _session.MediaPropertiesChanged += OnMediaPropertiesChanged;
        _session.PlaybackInfoChanged += OnPlaybackInfoChanged;
        _session.TimelinePropertiesChanged += OnTimelinePropertiesChanged;
        _ = PullFullStateAsync(_session);
    }

    private void DetachSession()
    {
        if (_session == null) return;
        _session.MediaPropertiesChanged -= OnMediaPropertiesChanged;
        _session.PlaybackInfoChanged -= OnPlaybackInfoChanged;
        _session.TimelinePropertiesChanged -= OnTimelinePropertiesChanged;
        _session = null;
    }

    private async void OnMediaPropertiesChanged(GlobalSystemMediaTransportControlsSession sender, MediaPropertiesChangedEventArgs args)
    {
        await UpdateMediaAsync(sender);
    }

    private async void OnPlaybackInfoChanged(GlobalSystemMediaTransportControlsSession sender, PlaybackInfoChangedEventArgs args)
    {
        await UpdatePlaybackAsync(sender);
    }

    private async void OnTimelinePropertiesChanged(GlobalSystemMediaTransportControlsSession sender, TimelinePropertiesChangedEventArgs args)
    {
        await UpdateTimelineAsync(sender);
    }

    private async Task PullFullStateAsync(GlobalSystemMediaTransportControlsSession session)
    {
        await UpdateMediaAsync(session);
        await UpdatePlaybackAsync(session);
        await UpdateTimelineAsync(session);
    }

    private async Task UpdateMediaAsync(GlobalSystemMediaTransportControlsSession session)
    {
        try
        {
            var props = await session.TryGetMediaPropertiesAsync();
            if (props == null) return;

            string title = props.Title ?? string.Empty;
            string artist = props.Artist ?? string.Empty;
            string album = props.AlbumTitle ?? string.Empty;
            string? ncmId = ExtractNcmId(props.Genres);

            string trackKey = $"{ncmId}|{title}|{artist}|{album}";
            bool sameTrack = trackKey == _lastTrackKey;
            if (!sameTrack)
            {
                _lastTrackKey = trackKey;
                _log.Info($"SMTC track: {title} - {artist} ({ncmId ?? "no-id"})");
                _lastTitle = title;
                _lastArtist = artist;
                ResetLyrics();
                _currentNcmId = null;
                if (props.Thumbnail != null)
                {
                    _ = SendCoverAsync(props);
                }
                else
                {
                    _log.Info("SMTC cover skipped: thumbnail null");
                }
                SendNowPlayingMeta(title, artist);
                if (!string.IsNullOrWhiteSpace(ncmId))
                {
                    _ = LoadLyricsAsync(ncmId);
                }
                return;
            }

            if (string.IsNullOrWhiteSpace(_lastTitle) && string.IsNullOrWhiteSpace(_lastArtist))
            {
                _lastTitle = title;
                _lastArtist = artist;
                SendNowPlayingMeta(title, artist);
            }

            bool needLyrics = false;
            lock (_lyricLock)
            {
                needLyrics = _lyrics.Count == 0;
            }
            if (!string.IsNullOrWhiteSpace(ncmId) && needLyrics)
            {
                _ = LoadLyricsAsync(ncmId);
            }

            if (_lastCover == null && props.Thumbnail != null)
            {
                _ = SendCoverAsync(props);
            }
        }
        catch (Exception ex)
        {
            _log.Info($"SMTC media update failed: {ex.Message}");
        }
    }

    private Task UpdatePlaybackAsync(GlobalSystemMediaTransportControlsSession session)
    {
        try
        {
            var info = session.GetPlaybackInfo();
            _log.Info($"SMTC state: {info.PlaybackStatus}");
            if (info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Stopped)
            {
                _serial.SendLine("LRC CLR");
                _serial.SendLine("NP CLR");
            }
        }
        catch (Exception ex)
        {
            _log.Info($"SMTC playback update failed: {ex.Message}");
        }
        return Task.CompletedTask;
    }

    private Task UpdateTimelineAsync(GlobalSystemMediaTransportControlsSession session)
    {
        try
        {
            var timeline = session.GetTimelineProperties();
            var posMs = (long)Math.Max(0, timeline.Position.TotalMilliseconds);
            var endMs = (long)Math.Max(0, timeline.EndTime.TotalMilliseconds);
            var maxSeekMs = (long)Math.Max(0, timeline.MaxSeekTime.TotalMilliseconds);
            var durMs = Math.Max(endMs, maxSeekMs);
            if (durMs <= 0 && DateTime.UtcNow - _lastTimelineWarn > TimeSpan.FromSeconds(10))
            {
                _lastTimelineWarn = DateTime.UtcNow;
                _log.Info($"SMTC timeline: pos={posMs} end={endMs} maxSeek={maxSeekMs}");
            }
            _lastPositionMs = posMs;
            UpdateLyricByPosition(posMs);
            if (Volatile.Read(ref _coverSending) == 0)
            {
                MaybeSendProgress(posMs, durMs);
            }
        }
        catch (Exception ex)
        {
            _log.Info($"SMTC timeline update failed: {ex.Message}");
        }
        return Task.CompletedTask;
    }

    private void ResetLyrics()
    {
        lock (_lyricLock)
        {
            _lyrics = new List<LyricEntry>();
            _lastLyricIndex = -2;
        }
        _serial.SendLine("LRC CLR");
        _serial.SendLine("NP CLR");
        _lastCover = null;
    }

    private async Task LoadLyricsAsync(string ncmId)
    {
        if (ncmId == _currentNcmId) return;
        _currentNcmId = ncmId;

        _lyricCts?.Cancel();
        _lyricCts = new CancellationTokenSource();
        var token = _lyricCts.Token;

        try
        {
            var url = $"https://music.163.com/api/song/lyric?id={ncmId}&lv=1&kv=1&tv=-1";
            using var resp = await _http.GetAsync(url, token);
            if (!resp.IsSuccessStatusCode)
            {
                _log.Info($"Lyric fetch failed: {resp.StatusCode}");
                return;
            }
            var json = await resp.Content.ReadAsStringAsync(token);
            string? lrc = ExtractLyricText(json);
            if (string.IsNullOrWhiteSpace(lrc))
            {
                _log.Info("Lyric empty");
                return;
            }

            var entries = ParseLrc(lrc);
            if (entries.Count == 0)
            {
                _log.Info("Lyric parsed empty");
                return;
            }

            lock (_lyricLock)
            {
                _lyrics = entries;
                _lastLyricIndex = -2;
            }
            _log.Info($"Lyric loaded: {entries.Count} lines");
            UpdateLyricByPosition(_lastPositionMs);
        }
        catch (OperationCanceledException)
        {
            // ignore
        }
        catch (Exception ex)
        {
            _log.Info($"Lyric fetch failed: {ex.Message}");
        }
    }

    private void SendNowPlayingMeta(string title, string artist)
    {
        string safeTitle = SanitizeField(title);
        string safeArtist = SanitizeField(artist);
        string metaKey = $"{safeTitle}\t{safeArtist}";
        if (metaKey == _lastMetaKey &&
            DateTime.UtcNow - _lastMetaSent < TimeSpan.FromSeconds(2))
        {
            return;
        }
        _lastMetaKey = metaKey;
        _lastMetaSent = DateTime.UtcNow;
        _log.Info($"Send NP META: {safeTitle} - {safeArtist}");
        _serial.SendLine($"NP META {safeTitle}\t{safeArtist}");
    }

    private void MaybeSendProgress(long posMs, long durMs)
    {
        if (durMs <= 0)
        {
            if (Math.Abs(posMs - _lastProgPos) < 200 &&
                DateTime.UtcNow - _lastProgSent < TimeSpan.FromMilliseconds(1000))
            {
                return;
            }
            _lastProgPos = posMs;
            _lastProgDur = durMs;
            _lastProgSent = DateTime.UtcNow;
            _log.Info($"Send NP PROG (no dur): pos={posMs} dur={durMs}");
            _serial.SendLine($"NP PROG {posMs} {durMs}");
            return;
        }
        if (Math.Abs(posMs - _lastProgPos) < 250 && durMs == _lastProgDur &&
            DateTime.UtcNow - _lastProgSent < TimeSpan.FromMilliseconds(500))
        {
            return;
        }

        _lastProgPos = posMs;
        _lastProgDur = durMs;
        _lastProgSent = DateTime.UtcNow;
        if (DateTime.UtcNow.Second % 5 == 0)
        {
            _log.Info($"Send NP PROG: pos={posMs} dur={durMs}");
        }
        _serial.SendLine($"NP PROG {posMs} {durMs}");
    }

    private static string SanitizeField(string text)
    {
        if (string.IsNullOrEmpty(text)) return string.Empty;
        return text.Replace("\r", " ").Replace("\n", " ").Replace("\t", " ").Trim();
    }

    private async Task SendCoverAsync(GlobalSystemMediaTransportControlsSessionMediaProperties props)
    {
        if (props.Thumbnail == null)
        {
            _log.Info("SMTC cover skipped: thumbnail null");
            return;
        }
        if (!await _coverLock.WaitAsync(0)) return;
        Interlocked.Exchange(ref _coverSending, 1);
        try
        {
            var pixels = await DecodeCoverAsync(props.Thumbnail);
            if (pixels == null) return;
            _lastCover = pixels;
            _log.Info("Send NP COV begin");

            _serial.SendLine("NP COV BEGIN 40 40");
            const int chunk = 100;
            var sb = new StringBuilder(chunk * 4);
            int count = 0;
            foreach (var px in pixels)
            {
                sb.Append(px.ToString("X4"));
                count++;
                if (count >= chunk)
                {
                    _serial.SendLine($"NP COV DATA {sb}");
                    sb.Clear();
                    count = 0;
                }
            }
            if (sb.Length > 0)
            {
                _serial.SendLine($"NP COV DATA {sb}");
            }
            _serial.SendLine("NP COV END");
            _log.Info($"Send NP COV end count={pixels.Length}");
        }
        catch (Exception ex)
        {
            _log.Info($"Cover send failed: {ex.Message}");
        }
        finally
        {
            Interlocked.Exchange(ref _coverSending, 0);
            _coverLock.Release();
        }
    }

    public void ResendNowPlaying()
    {
        bool hasMeta = !(string.IsNullOrWhiteSpace(_lastTitle) && string.IsNullOrWhiteSpace(_lastArtist));
        if (hasMeta)
        {
            SendNowPlayingMeta(_lastTitle, _lastArtist);
        }
        if (_lastProgDur > 0 && _lastProgPos >= 0)
        {
            MaybeSendProgress(_lastProgPos, _lastProgDur);
        }

        if (_lastCover != null)
        {
            Interlocked.Exchange(ref _coverSending, 1);
            _serial.SendLine("NP COV BEGIN 40 40");
            const int chunk = 100;
            var sb = new StringBuilder(chunk * 4);
            int count = 0;
            foreach (var px in _lastCover)
            {
                sb.Append(px.ToString("X4"));
                count++;
                if (count >= chunk)
                {
                    _serial.SendLine($"NP COV DATA {sb}");
                    sb.Clear();
                    count = 0;
                }
            }
            if (sb.Length > 0)
            {
                _serial.SendLine($"NP COV DATA {sb}");
            }
            _serial.SendLine("NP COV END");
            Interlocked.Exchange(ref _coverSending, 0);
        }
        bool needRefresh = !hasMeta || _lastCover == null || _lastProgDur <= 0;
        if (needRefresh && _session != null)
        {
            _ = PullFullStateAsync(_session);
        }
    }

    private static async Task<ushort[]?> DecodeCoverAsync(IRandomAccessStreamReference thumbnail)
    {
        using IRandomAccessStream stream = await thumbnail.OpenReadAsync();
        BitmapDecoder decoder = await BitmapDecoder.CreateAsync(stream);
        var transform = new BitmapTransform
        {
            ScaledWidth = 40,
            ScaledHeight = 40,
            InterpolationMode = BitmapInterpolationMode.NearestNeighbor
        };
        PixelDataProvider data = await decoder.GetPixelDataAsync(
            BitmapPixelFormat.Bgra8,
            BitmapAlphaMode.Ignore,
            transform,
            ExifOrientationMode.IgnoreExifOrientation,
            ColorManagementMode.DoNotColorManage);
        byte[] bytes = data.DetachPixelData();
        if (bytes.Length < 40 * 40 * 4) return null;

        var result = new ushort[40 * 40];
        int di = 0;
        for (int i = 0; i < bytes.Length; i += 4)
        {
            byte b = bytes[i];
            byte g = bytes[i + 1];
            byte r = bytes[i + 2];
            // Panel expects RGB565 (same as UI colors).
            ushort rgb565 = (ushort)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            result[di++] = rgb565;
            if (di >= result.Length) break;
        }
        return result;
    }

    private void UpdateLyricByPosition(long positionMs)
    {
        List<LyricEntry> list;
        lock (_lyricLock)
        {
            list = _lyrics;
        }
        if (list.Count == 0 || positionMs < 0) return;

        int idx = FindLyricIndex(list, positionMs);
        if (idx == _lastLyricIndex) return;
        _lastLyricIndex = idx;

        if (idx < 0)
        {
            _serial.SendLine("LRC CLR");
            return;
        }

        string cur = list[idx].Text;
        _serial.SendLine($"LRC CUR {cur}");
        if (idx + 1 < list.Count)
        {
            _serial.SendLine($"LRC NXT {list[idx + 1].Text}");
        }
        else
        {
            _serial.SendLine("LRC NXT ");
        }
    }

    private static int FindLyricIndex(List<LyricEntry> list, long positionMs)
    {
        int lo = 0;
        int hi = list.Count - 1;
        int idx = -1;
        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            if (list[mid].TimeMs <= positionMs)
            {
                idx = mid;
                lo = mid + 1;
            }
            else
            {
                hi = mid - 1;
            }
        }
        return idx;
    }

    private static string? ExtractLyricText(string json)
    {
        using var doc = JsonDocument.Parse(json);
        if (!doc.RootElement.TryGetProperty("lrc", out var lrcElement)) return null;
        if (!lrcElement.TryGetProperty("lyric", out var lyricElement)) return null;
        return lyricElement.GetString();
    }

    private static readonly Regex LrcTagRegex = new(@"\[(\d+):(\d+)(?:\.(\d{1,3}))?\]", RegexOptions.Compiled);

    private static List<LyricEntry> ParseLrc(string lrc)
    {
        var result = new List<LyricEntry>();
        var lines = lrc.Split(new[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries);
        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (line.Length == 0) continue;
            var matches = LrcTagRegex.Matches(line);
            if (matches.Count == 0) continue;
            string text = LrcTagRegex.Replace(line, "").Trim();
            foreach (Match match in matches)
            {
                if (!int.TryParse(match.Groups[1].Value, out int mm)) continue;
                if (!int.TryParse(match.Groups[2].Value, out int ss)) continue;
                int ms = 0;
                if (match.Groups[3].Success)
                {
                    string frac = match.Groups[3].Value;
                    if (frac.Length == 1) ms = int.Parse(frac) * 100;
                    else if (frac.Length == 2) ms = int.Parse(frac) * 10;
                    else ms = int.Parse(frac.Substring(0, Math.Min(3, frac.Length)));
                }
                long timeMs = (mm * 60 + ss) * 1000L + ms;
                if (timeMs < 0) continue;
                result.Add(new LyricEntry(timeMs, text));
            }
        }
        result.Sort((a, b) => a.TimeMs.CompareTo(b.TimeMs));
        return result;
    }

    private sealed record LyricEntry(long TimeMs, string Text);

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        StopTimelineTimer();
        _timelineTimer.Dispose();
        DetachSession();
        if (_manager != null)
        {
            _manager.SessionsChanged -= OnSessionsChanged;
            _manager.CurrentSessionChanged -= OnCurrentSessionChanged;
        }
    }
}
