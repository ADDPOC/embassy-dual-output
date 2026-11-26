# Embassy Dual Output (OBS plugin)

Stream **landscape 16:9 and portrait 9:16 at the same time** from one OBS, to as many platforms as you want.

- **Portrait Canvas dock**: a live 9:16 preview, its own portrait scenes, and quick layouts (full screen, top half, bottom half, cam on top).
- **Dual Output Streams dock**: destinations and stream keys for:
  - **Portrait**: TikTok, YouTube Shorts, Reels.
  - **Extra landscape**: Kick, YouTube and others. This replaces SE.Live.
- Each group is **encoded once on your GPU**, then sent to every destination you tick. Adding a platform only uses more upload bandwidth, not more GPU.
- Can start and stop with OBS's normal **Start Streaming** button.

Requires **OBS Studio 33 or newer** (uses the OBS 33 plugin folder layout) on Windows 10/11 (64-bit).

---

## 1. Build it with GitHub (no programming tools needed)

1. Sign in at github.com, click **+** (top right), then **New repository**.
   Name it `embassy-dual-output`, choose **Private** and click **Create repository**.
2. On the new repo page, click **uploading an existing file**.
3. Unzip `embassy-dual-output.zip` on your PC. Open the folder and drag **everything inside it** into the browser, including the `.github` folder.
   (If Windows hides `.github`: in File Explorer turn on **View → Show → Hidden items**.)
4. Click **Commit changes**.
5. Open the **Actions** tab. A build called *push run* starts by itself and takes about 10–15 minutes.
   - If Actions asks you to enable workflows, click the green button.
6. When it shows a green check, click the run. Scroll to **Artifacts** and download the file ending in **`windows-x64`**.

## 2. Install it

1. **Close OBS.**
2. Unzip the artifact. There's another zip inside, so unzip that too. Inside is a folder (`embassy-dual-output`) with the `.dll` file and a `data` folder.
3. Copy that folder into:
   `C:\ProgramData\obs-studio\plugins\`
   The result should be `C:\ProgramData\obs-studio\plugins\embassy-dual-output\embassy-dual-output.dll` (OBS 33 plugin layout).
4. Start OBS. Open **Docks** in the top menu and tick **Portrait Canvas** and **Dual Output Streams**.

> **Important:** remove the **SE.Live / StreamElements plugin** and **Aitum Vertical** first. Each of them also adds a vertical canvas, and that conflict is what has been crashing your OBS. Uninstall them from Windows *Apps*. If OBS still crashes afterwards, reinstall OBS (keep the "delete settings" box unchecked).

## 3. Set up your portrait layout

1. In **Portrait Canvas**, a scene called *Portrait Scene* already exists.
2. Click **+ Source** and pick your camera (e.g. EMEET Pixy), game capture or capture card.
   - You can also add a **whole landscape scene** (e.g. your gameplay scene).
   - New sources start full screen.
3. Select a source and click a layout button. A red box in the preview shows which source you selected.
   - Typical setup: camera → **Top third (cam)**, gameplay → **Bottom two-thirds**.
   - For exact placement, type X / Y / W / H and click **Apply box**. The canvas is 1080 wide × 1920 tall.
4. Tick or untick a source to show or hide it. **Up/Down** changes which source is in front.
5. Use **+ Scene** to make more portrait scenes, e.g. "Just Chatting" and "Gameplay". Clicking a scene puts it live on the portrait stream.

Sources are created in OBS's normal **Sources** panel first. A source that should only appear in portrait can live in a hidden helper scene, e.g. "Portrait assets". Audio always comes from your normal OBS audio mixer.

## 4. Add destinations and go live

The first time you open it, three destinations are already listed, all switched off until they have a key:
- **Portrait:** TikTok
- **Extra landscape:** YouTube and Kick
- **Twitch** stays in OBS's normal **Settings → Stream**.

1. Paste each platform's **Server URL** into its row. YouTube's is already filled in. Use **+ Destination** to add more platforms.
2. Copy the **stream key** from that platform's live dashboard and click **Paste key** on its row. This also switches the destination on.
   - **New TikTok key while you're already live?** Copy it and click **Paste key**. Only TikTok reconnects; Twitch, YouTube and Kick keep streaming.
   - **TikTok** only gives an RTMP server and key to accounts that have access. If you don't see one in TikTok LIVE Studio or LIVE Center, keep using TikTok LIVE Studio for TikTok.
   - **YouTube** vertical: create a separate stream in YouTube Studio and use its key.
3. Recommended settings for your RX 9060 XT: encoder **AMD AMF H.264**.
   - Portrait: **6000 kbps**. Landscape: 6000–8000 kbps.
4. Either click **Start**, or leave *"Start and stop together with OBS's Start Streaming button"* ticked and use OBS's normal button.
5. The status line shows **LIVE**, **Reconnecting…** or an error for each destination.

Keep your main platform (e.g. Twitch) in OBS **Settings → Stream** as usual. Use **Extra landscape** for the other platforms.

Upload guide: each destination uses roughly its bitrate. Example: Twitch 6000 + Kick 6000 + TikTok 6000 + Shorts 6000 = about 24 Mbps upload.

## Where things are saved

- Portrait scenes are saved with your OBS **scene collection**.
- Destinations and stream keys are saved in
  `%APPDATA%\obs-studio\plugin_config\embassy-dual-output\streams.json`. Treat that file like a password.

## If something goes wrong

In OBS, go to **Help → Log Files → Upload Current Log File** and send the link. Lines starting with `[embassy-dual-output]` come from this plugin.

## License

GPL-2.0, like OBS itself. Anything that loads inside OBS must be GPL-compatible. Keep paid closed-source products (like Embassy Studio) as separate programs that talk to OBS.
