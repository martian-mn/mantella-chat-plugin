// Mantella in-game chat — PrismaUI bridge + transcript polling.
//
// The SKSE plugin polls the Mantella Python server for new transcript
// entries and pushes them in via window.mantellaUpdate(). When the user
// submits text, the form calls window.mantella.submitInput(text) which the
// SKSE plugin has registered as a JS listener. The plugin POSTs to the
// Python server's /chat/input endpoint.
//
// All HTTP traffic happens C++-side (no fetch() here) so we don't deal with
// CORS or Ultralight quirks.

(function () {
  "use strict";

  const chat = document.getElementById("chat");
  const transcriptEl = document.getElementById("transcript");
  const statusEl = document.getElementById("status");
  const form = document.getElementById("form");
  const input = document.getElementById("input");
  const sendBtn = document.getElementById("send");

  // Track which entries we've already rendered so updates are idempotent.
  // Key by timestamp; collisions on identical timestamps are unlikely since
  // the server uses time.time() with floating-point precision.
  const rendered = new Set();

  function setStatus(text) {
    statusEl.textContent = text;
  }

  function setActive(active) {
    chat.dataset.state = active ? "active" : "idle";
  }

  function renderEntry(entry) {
    const key = entry.timestamp + "|" + entry.kind + "|" + entry.text.slice(0, 32);
    if (rendered.has(key)) return;
    rendered.add(key);

    const div = document.createElement("div");
    div.className = "entry entry--" + entry.kind;

    if (entry.kind === "system") {
      div.textContent = entry.text;
    } else {
      const speaker = document.createElement("span");
      speaker.className = "entry__speaker";
      speaker.textContent = entry.speaker + ":";
      const text = document.createElement("span");
      text.className = "entry__text";
      text.textContent = " " + entry.text;
      div.appendChild(speaker);
      div.appendChild(text);
    }

    transcriptEl.appendChild(div);

    // Auto-scroll to latest unless user has scrolled up
    const nearBottom =
      transcriptEl.scrollHeight - transcriptEl.scrollTop - transcriptEl.clientHeight < 80;
    if (nearBottom) {
      transcriptEl.scrollTop = transcriptEl.scrollHeight;
    }
  }

  // Bridge: SKSE plugin pushes transcript entries here on each poll.
  // Argument is JSON-serialized list of {speaker, text, kind, timestamp}.
  window.mantellaUpdate = function (json) {
    let entries;
    try {
      entries = typeof json === "string" ? JSON.parse(json) : json;
    } catch (e) {
      console.error("mantellaUpdate parse error", e);
      return;
    }
    if (!Array.isArray(entries)) return;
    for (const entry of entries) {
      renderEntry(entry);
    }
    if (entries.length > 0) {
      setActive(true);
    }
  };

  // Bridge: SKSE plugin sets the status text (idle / waiting / typing / error)
  window.mantellaStatus = function (text) {
    setStatus(text);
    if (text === "waiting") {
      setActive(true);
    } else if (text === "idle") {
      setActive(false);
    }
  };

  // Bridge: SKSE plugin clears the chat (e.g. on conversation end)
  window.mantellaClear = function () {
    transcriptEl.innerHTML = "";
    rendered.clear();
    setActive(false);
    setStatus("idle");
  };

  // Bridge: SKSE plugin grants/removes input focus.
  // Under Ultralight + Skyrim's input grab, the normal <input>.focus() chain
  // doesn't always reliably receive keystrokes. We track focus ourselves and
  // route keys via a document-level keydown listener that mirrors the typed
  // text into the input's value.
  let panelActive = false;

  window.mantellaFocus = function (focused) {
    panelActive = !!focused;
    if (focused) {
      input.focus();
      setActive(true);
    } else {
      input.blur();
      setActive(false);
    }
  };

  // Click anywhere on the chat panel to ask SKSE to grant focus.
  // Mouse events route through Ultralight reliably; sometimes that engages
  // the input dispatcher even when calling Focus() from C++ directly does not.
  chat.addEventListener("click", function () {
    if (typeof window.mantellaRequestFocus === "function") {
      window.mantellaRequestFocus("");
    }
  });

  // Bridge: SKSE plugin forwards keystrokes here (one call per key press).
  // We bypass PrismaUI's WndProc-based input router because under Wine +
  // Proton + gamescope WM_KEYDOWN messages don't reach Ultralight's hook
  // even when Focus() succeeds. The C++ InputHandler captures keys via
  // BSInputDeviceManager (the same path Skyrim itself uses for movement)
  // and calls this function with a JavaScript-style key value.
  //
  // We construct a synthetic KeyboardEvent and dispatch it on the document
  // so the existing keydown handler below treats it identically to a real
  // OS-delivered event.
  window.mantellaInjectKey = function (key, shift, ctrl) {
    const ev = new KeyboardEvent("keydown", {
      key: key,
      shiftKey: !!shift,
      ctrlKey: !!ctrl,
      bubbles: true,
      cancelable: true
    });
    document.dispatchEvent(ev);
  };

  document.addEventListener("keydown", function (e) {
    if (!panelActive) return;

    if (e.key === "Escape") {
      e.preventDefault();
      // Tell SKSE plugin to release focus back to the game.
      if (typeof window.mantellaRequestUnfocus === "function") {
        window.mantellaRequestUnfocus("");
      }
      return;
    }

    if (e.key === "Enter") {
      e.preventDefault();
      // Trigger the same submit path as clicking Send.
      form.dispatchEvent(new Event("submit", { cancelable: true }));
      return;
    }

    if (e.key === "Backspace") {
      e.preventDefault();
      input.value = input.value.slice(0, -1);
      return;
    }

    // Single printable character — append to the input value.
    // (e.key is "a", " ", "?" etc. for printable keys; multi-char names like
    // "Shift", "Control", "ArrowLeft" are filtered out by the length check.)
    if (e.key.length === 1) {
      e.preventDefault();
      input.value += e.key;
    }
  });

  // Outbound: user submits a message.
  form.addEventListener("submit", function (e) {
    e.preventDefault();
    const text = input.value.trim();
    if (!text) return;
    input.value = "";
    sendBtn.disabled = true;
    setStatus("submitted");

    if (typeof window.mantellaSubmitInput === "function") {
      window.mantellaSubmitInput(text);
    } else {
      console.error("SKSE bridge mantellaSubmitInput is not available");
    }

    // Re-enable Send after a short cooldown so users can't spam.
    setTimeout(function () {
      sendBtn.disabled = false;
    }, 250);
  });

  // Stub the SKSE-registered listener until the plugin attaches it.
  // Calling this when the plugin isn't loaded is harmless.
  if (typeof window.mantellaSubmitInput !== "function") {
    window.mantellaSubmitInput = function (_text) {
      console.warn("SKSE plugin not connected; input dropped");
    };
  }

  // Default: idle state at startup.
  setStatus("idle");
  setActive(false);
})();
