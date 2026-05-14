# VitaNotify

# 🚀 VitaNetNotifier

**A seamless, native bridge for sending PC desktop notifications directly to your PS Vita.**

VitaNetNotifier is a two-part project consisting of a **PS Vita taiHEN plugin** and a **Python desktop daemon**. Instead of relying on clunky third-party overlay apps, this project uses dynamic reverse-engineering techniques to intercept the PS Vita's internal database and inject notifications directly into the console's native UI.

If you get a Discord message, a Telegram ping, or a Spotify track change on your PC, it instantly pops up on your PS Vita screen.

---

## ⚠️ Compatibility
**This plugin has been tested and confirmed working on Custom Firmware (CFW) 3.65.** 
Compatibility with other firmware versions (such as 3.60 or 3.68+) is not guaranteed, as internal system offsets and database behaviors may vary.

---

## ✨ Features

*   **100% Native UI:** Uses the official PS Vita notification system. No custom overlays, no visual clutter.
*   **Dynamic DB Hooking:** Elegantly hooks into the system's internal database module to capture the notification handler on the fly.
*   **App-to-TitleID Mapping:** PC applications are mapped to specific PS Vita Title IDs (e.g., your PC Telegram notifications can trigger the PS Vita's browser or messaging icon).
*   **Smart Debouncing:** Built-in spam protection prevents duplicate notifications from flooding your console.
*   **Zero Polling:** Uses a persistent, lightweight TCP stream for instant, battery-friendly delivery.

---

## 🧠 Under the Hood: The Magic

The core of this project relies on a "Clean Room" reverse-engineering approach to bypass the lack of a public API for system notifications.

Instead of trying to parse proprietary file formats or memory structures, the Vita plugin hooks into the internal database engine. When the PS Vita's OS interacts with its notification tables, the plugin intercepts the call, quietly retrieves the active database handle, and safely unhooks itself. 

Once the handle is captured, the plugin listens on a TCP socket and performs real-time injections using the console's own internal functions to push new events to the UI.

---

## 🌐 Network Configuration

Before compiling the plugin or running the server, you need to set up your network configuration statically in the source code.

| Setting | Where to Configure | Example Value | Description |
| :--- | :--- | :--- | :--- |
| **Server IP** | C Plugin (`network_thread`) | `192.168.1.24` | The static local IP address of the PC/Server sending the notifications. |
| **TCP Port** | C Plugin & Server Script | `4034` | The port used for communication. **Must match** on both the Vita and the Server. |
| **Bind Host** | Server Script (`HOST`) | `0.0.0.0` | Instructs the server to listen on all available network interfaces. |


## 🛠️ Installation & Setup

### Part 1: The PS Vita Plugin (C)

**Prerequisites:** A modded PS Vita with HENkaku/Enso (CFW 3.65) and VitaSDK installed on your PC (if compiling from source).

1. Compile the plugin using VitaSDK to generate the `.suprx` file, or download the pre-compiled release.
2. Transfer the `.suprx` file to your PS Vita (e.g., `ur0:/tai/vitanetnotifier.suprx`).
3. Open your `ur0:/tai/config.txt` and add the plugin under the `*main` section (since it interacts with the core OS):

    ```text
    *main
    ur0:tai/vitanetnotifier.suprx

4. Reboot your PS Vita.


### Part 2: The Desktop Server
**Note:** The provided Python script is just a reference implementation. You can use any server (written in Node.js, Go, C#, etc.) as long as it accepts TCP connections and sends raw string payloads formatted as TitleID|Message\0.

Using the provided Python Daemon (Linux DBus example):

1. Install the required Python DBus libraries (e.g., on Ubuntu: sudo apt install python3-dbus python3-gi).

2. Open the server.py file and ensure the PORT matches the one compiled in your Vita plugin.

3. Update the APP_ID_MAP dictionary in the script to map your favorite apps to PS Vita Title IDs:
  ```code
APP_ID_MAP = {
    "Discord": "PCSE00265",
    "Telegram Desktop": "NPXS10001"
}
```

4. Run the daemon:
  ```bash
      python3 server.py
  ```

*Make sure your PS Vita and PC are on the same local network. The daemon will automatically detect when the Vita connects to the TCP socket.*
---
## ⚖️ Disclaimer & Legal

This project is intended strictly for **educational purposes and interoperability**. 
* It does not contain, distribute, or require proprietary Sony code, decryption keys, or copyrighted assets. 
* The techniques used rely purely on open-source community tools (VitaSDK, taiHEN) to interface with the operating system dynamically.
* This tool cannot be used to bypass DRM or facilitate piracy.

---

## 🤝 Contributing

Pull requests are welcome! If you find new ways to customize the notification parameters or want to add support for Windows/macOS notification bridging, feel free to fork the repository.
