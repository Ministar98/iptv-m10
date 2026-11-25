"use strict";

const HLS_URL = "/runtime/hls/index.m3u8";

const video = document.getElementById("video");

const statusElement =
    document.getElementById("status");

const playbackTimeElement =
    document.getElementById("playback-time");

const resolutionElement =
    document.getElementById("resolution");

const startButton =
    document.getElementById("start-button");

const stopButton =
    document.getElementById("stop-button");

let hls = null;

let playbackActive = false;


function setStatus(message) {

    statusElement.textContent = message;

    console.log("[IPTV M10]", message);
}


function stopPlayback() {

    playbackActive = false;

    if (hls !== null) {

        hls.destroy();

        hls = null;
    }

    video.pause();

    video.removeAttribute("src");

    video.load();

    playbackTimeElement.textContent = "0.00 s";

    resolutionElement.textContent = "Unknown";

    setStatus("Stopped");
}


function playVideo() {

    video.play().catch((error) => {

        if (!playbackActive) {
            return;
        }

        console.warn("Playback request:", error);

        setStatus("Press Play in video controls");
    });
}


function startPlayback() {

    stopPlayback();

    playbackActive = true;

    setStatus("Loading HLS stream");

    if (
        typeof Hls !== "undefined" &&
        Hls.isSupported()
    ) {

        hls = new Hls({

            lowLatencyMode: false,

            liveSyncDurationCount: 3,

            liveMaxLatencyDurationCount: 5,

            maxBufferLength: 10,

            backBufferLength: 10
        });

        hls.on(
            Hls.Events.MEDIA_ATTACHED,
            () => {

                hls.loadSource(HLS_URL);
            }
        );

        hls.on(
            Hls.Events.MANIFEST_PARSED,
            () => {

                setStatus("HLS manifest loaded");

                playVideo();
            }
        );

        hls.on(
            Hls.Events.ERROR,
            (event, data) => {

                console.warn("HLS error:", data);

                if (!data.fatal) {
                    return;
                }

                const message =
                    `HLS error: ${data.details}`;

                stopPlayback();

                setStatus(message);
            }
        );

        hls.attachMedia(video);

    } else if (
        video.canPlayType(
            "application/vnd.apple.mpegurl"
        )
    ) {

        video.src = HLS_URL;

        playVideo();

    } else {

        playbackActive = false;

        setStatus("HLS playback is not supported");
    }
}


startButton.addEventListener(
    "click",
    startPlayback
);

stopButton.addEventListener(
    "click",
    stopPlayback
);


video.addEventListener("playing", () => {

    if (playbackActive) {

        setStatus("Playing");
    }
});


video.addEventListener("pause", () => {

    if (playbackActive) {

        setStatus("Paused");
    }
});


video.addEventListener("waiting", () => {

    if (playbackActive) {

        setStatus("Buffering");
    }
});


video.addEventListener("ended", () => {

    if (playbackActive) {

        setStatus("Playback ended");
    }
});


video.addEventListener("timeupdate", () => {

    if (!playbackActive) {
        return;
    }

    playbackTimeElement.textContent =
        `${video.currentTime.toFixed(2)} s`;
});


video.addEventListener("loadedmetadata", () => {

    if (!playbackActive) {
        return;
    }

    resolutionElement.textContent =
        `${video.videoWidth} x ${video.videoHeight}`;
});


video.addEventListener("error", () => {

    if (playbackActive && video.error) {

        setStatus(
            `Video error: ${video.error.code}`
        );
    }
});


window.addEventListener("beforeunload", () => {

    if (hls !== null) {

        hls.destroy();
    }
});
