"use strict";
const HLS_URL = "/runtime/hls/index.m3u8";
const $ = id => document.getElementById(id);
const video = $("video");
const timeline = $("timeline");
const seekInput = $("seek-position");
const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);
const seconds = v => Number.isFinite(v) ? `${v.toFixed(2)} s` : "--";
let hls = null, details = null, timer = null;
let active = false, pausedPosition = null, pending = null, dragging = false;
let dragInterval = null;
function status(message) { $("status").textContent = message; console.log("[IPTV M10]", message); }
function mode(value, css = "") { $("playback-mode").textContent = value; $("playback-mode").className = `metric-value ${css}`; }
function interval() {
    const fragments = details?.fragments;
    if (!fragments?.length) return null;
    const first = fragments[0], last = fragments[fragments.length - 1];
    const start = first.start, end = last.start + last.duration;
    return Number.isFinite(start) && Number.isFinite(end) && end > start ? {start, end} : null;
}
function livePosition(w) {
    const p = hls?.liveSyncPosition;
    return clamp(Number.isFinite(p) ? p : w.end - 6, w.start, w.end);
}
function buttons() {
    const ready = active && video.readyState >= 1 && !!interval();
    $("start-button").disabled = active;
    $("stop-button").disabled = !active;
    $("pause-button").disabled = !ready || !!pending || video.paused;
    $("resume-button").disabled = !ready || !!pending || !video.paused;
    for (const id of ["oldest-button", "live-button", "seek-button", "seek-position", "timeline"]) $(id).disabled = !ready || !!pending;
}
function update() {
    buttons();
    if (!active) return;
    const w = interval();
    if (!w) { mode("WAITING"); return; }
    const p = pausedPosition ?? video.currentTime, live = livePosition(w);
    const values = {"buffer-start": w.start, "buffer-end": w.end, "buffer-duration": w.end-w.start,
        "playback-position": p, "live-position": live, "timeshift-delay": Math.max(0,live-p),
        "live-edge-distance": Math.max(0,w.end-p), "playback-time": p};
    for (const [id,v] of Object.entries(values)) $(id).textContent = seconds(v);
    if (!dragging) {
        timeline.min = String(w.start); timeline.max = String(Math.max(w.start,live));
        timeline.value = String(clamp(p,w.start,Math.max(w.start,live)));
        $("timeline-start").textContent = seconds(w.start);
        $("timeline-end").textContent = `Live ${seconds(live)}`;
    }
    if (pending) mode("SEEKING", "mode-seeking");
    else if (video.readyState === 0) mode("WAITING");
    else if (p < w.start) mode("EXPIRED", "mode-expired");
    else if (video.paused) mode("PAUSED", "mode-paused");
    else if (live-p <= 2) mode("LIVE", "mode-live");
    else mode("TIMESHIFT", "mode-timeshift");
}
function stopPlayback() {
    active = false; pending = null; pausedPosition = null; details = null; dragging = false; dragInterval = null;
    clearInterval(timer); timer = null;
    const old = hls; hls = null; if (old) old.destroy();
    video.pause(); video.removeAttribute("src"); video.load();
    for (const id of ["buffer-start","buffer-end","buffer-duration","playback-position","live-position","timeshift-delay","live-edge-distance","timeline-start","timeline-end"]) $(id).textContent = "--";
    $("playback-time").textContent = "0.00 s"; $("resolution").textContent = "Unknown";
    seekInput.value = ""; timeline.min = "0"; timeline.max = "1"; timeline.value = "0";
    mode("IDLE"); status("Stopped"); buttons();
}
function playVideo() {
    const instance = hls;
    video.play().catch(error => { if (active && hls === instance) { console.warn(error); status("Playback blocked; press Resume"); buttons(); } });
}
function startPlayback() {
    stopPlayback();
    if (typeof Hls === "undefined" || !Hls.isSupported()) { status("HLS.js / MSE not supported"); return; }
    active = true;
    const instance = new Hls({lowLatencyMode:false, liveSyncDurationCount:3, liveMaxLatencyDurationCount:Infinity,
        maxLiveSyncPlaybackRate:1, maxBufferLength:10, backBufferLength:10});
    hls = instance;
    instance.on(Hls.Events.MEDIA_ATTACHED, () => { if (hls === instance) instance.loadSource(HLS_URL); });
    instance.on(Hls.Events.MANIFEST_PARSED, () => { if (hls === instance) playVideo(); });
    instance.on(Hls.Events.LEVEL_UPDATED, (_, data) => { if (hls === instance) { details = data.details; update(); } });
    instance.on(Hls.Events.ERROR, (_, data) => { if (hls !== instance) return; console.warn("HLS",data); if (data.fatal) { stopPlayback(); status(`HLS error: ${data.details}`); } });
    instance.attachMedia(video); status("Loading HLS stream"); timer = setInterval(tick,250); update();
}
function pausePlayback() {
    if (!active || pending || video.paused || video.readyState === 0) return;
    pausedPosition = video.currentTime; video.pause(); status("Paused"); update();
}
function seekToPosition(requested, reason = "Seek") {
    const w = interval();
    if (!active || !hls || !w || !Number.isFinite(requested)) return;
    const live = livePosition(w);
    // A small margin reduces the race with playlist eviction.
    const oldest = Math.min(w.start + 0.5, live);
    const target = clamp(requested, oldest, live);
    pending = {requested, target, reason, attempts: 0, started: performance.now()};
    pausedPosition = null;
    issueSeek();
}
function issueSeek() {
    const w = interval(); if (!pending || !w || !hls) return;
    pending.target = clamp(pending.requested, Math.min(w.start+0.5,livePosition(w)), livePosition(w));
    pending.attempts++; pending.started = performance.now();
    const target = pending.target;
    status(`${pending.reason}: ${seconds(target)}`);
    console.log("[IPTV M10] seek", {requested:pending.requested,target,window:w,attempt:pending.attempts});
    video.pause();
    hls.stopLoad();
    video.currentTime = target;
    hls.startLoad(target, true);
    playVideo(); update();
}
function tick() {
    if (!active) return;
    if (pending) {
        const w = interval();
        const elapsed = performance.now()-pending.started;
        const error = Math.abs(video.currentTime-pending.target);
        if (w && !video.seeking && !video.paused && video.readyState >= 3 && error <= 2 && video.currentTime >= w.start-0.1) {
            console.log("[IPTV M10] seek confirmed", {target:pending.target,actual:video.currentTime,error});
            pending = null; status("Playing");
        } else if (elapsed > 4000 || (w && pending.target < w.start)) {
            if (pending.attempts < 3) issueSeek();
            else { pending = null; video.pause(); pausedPosition = video.currentTime; status("Seek failed; select a position and retry"); }
        }
    }
    update();
}
function resumePlayback() {
    if (!active || pending || !video.paused) return;
    const w = interval(); if (!w) return;
    const p = pausedPosition ?? video.currentTime;
    if (p < w.start || p > w.end || Math.abs(video.currentTime-p)>0.05) seekToPosition(p,"Resume");
    else { pausedPosition = null; playVideo(); update(); }
}
$("start-button").onclick = startPlayback;
$("stop-button").onclick = stopPlayback;
$("pause-button").onclick = pausePlayback;
$("resume-button").onclick = resumePlayback;
$("oldest-button").onclick = () => { const w = interval(); if(w) seekToPosition(w.start,"Oldest"); };
$("live-button").onclick = () => { const w = interval(); if(w) seekToPosition(livePosition(w),"Go Live"); };
$("seek-button").onclick = () => { if(seekInput.value.trim() !== "") seekToPosition(Number(seekInput.value)); };
timeline.addEventListener("pointerdown", () => { dragging = true; dragInterval = interval(); });
timeline.addEventListener("input", () => { seekInput.value = timeline.value; });
timeline.addEventListener("change", () => { const target = Number(timeline.value); dragging = false; dragInterval = null; seekToPosition(target); });
window.addEventListener("pointerup", () => { dragging = false; dragInterval = null; });
window.addEventListener("pointercancel", () => { dragging = false; dragInterval = null; });
$("mute").onchange = e => { video.muted = e.target.checked; };
$("volume").oninput = e => { video.volume = Number(e.target.value); };
video.addEventListener("playing", () => { if(active && !pending) status("Playing"); update(); });
video.addEventListener("waiting", () => { if(active && !pending) status("Buffering"); });
video.addEventListener("loadedmetadata", () => { if(active) { $("resolution").textContent = `${video.videoWidth} x ${video.videoHeight}`; update(); } });
video.addEventListener("error", () => { if(active && video.error) status(`Video error: ${video.error.code}`); });
window.addEventListener("beforeunload", () => { clearInterval(timer); if(hls) hls.destroy(); });
buttons();
