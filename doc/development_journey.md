# Building Treadmill Connect: A Development Journey

**Project:** Treadmill Connect (iFit to FTMS Bridge)  
**Time Spent:** Approximately 3 Days (20-25 Hours of active development)  
**Team:** Github User & Antigravity (Google Deepmind AI Agent)

## 🏗️ How It Was Built

This project is the result of a "pair programming" collaboration between a human developer and an AI agent. The goal was to reverse-engineer a proprietary, closed-source Bluetooth protocol and re-implement it as an open standard.

### ⚠️ Previous Attempts (The "Claude" Phase)

Before this successful iteration, we spent approximately 2-3 days attempting to solve this problem using **Claude 3.5 Sonnet (Thinking)** as the primary AI agent.
*   **Outcome**: Failure.
*   **Issues**: The agent struggled significantly with the binary pattern recognition required for the custom checksums and often hallucinated standard Bluetooth protocols (like FTMS) where none existed. It could not isolate the specific handshake sequence required to "unlock" the proprietary motor controller.
*   **Pivot**: We started completely from scratch with **Gemini 3.0 Pro** (via Antigravity), effectively discarding the previous codebase and analysis.

### 🛠️ Tools Used


1.  **AI Agent**: **Antigravity** (Powered by Google's **Gemini 3.0 Pro** models).
    *   *Role*: Protocol analysis, hex dump parsing, code generation, debugging strategy.
2.  **IDE**: **VS Code**.
3.  **Packet Analysis**:
    *   **PacketLogger** (macOS): Used to capture raw Bluetooth traffic from the official iFit app to the treadmill.
    *   **Wireshark**: For deep inspection of PCAP files.
    *   **Custom Python Scripts**: We built our own parser (`pklg_protocol_parser.py`) to dissect the specific framing of the iFit packets.
4.  **Hardware**:
    *   MacBook Pro (Development/Host).
    *   NordicTrack iFit Treadmill.
    *   ESP32-S3 (Final embedded target).
    *   Google Pixel (Running the iFit app for capture).

### 🤖 Models & AI Contribution

The project relied heavily on **Google's Gemini 3.0 Pro** models (accessed via the Antigravity agent) for:

*   **Pattern Recognition**: Identifying repeating header sequences (`FE 02 ...`) in thousands of lines of hex dumps.
*   **Anomaly Detection**: Spotting the single "Enable" command (`0x90`) hidden among hundreds of telemetry packets that turned out to be the key to unlocking the motor.
*   **Code Translation**: Converting raw hex logic into structured Python (`struct.pack`) and C++ (for ESP32).

## 📅 The Progression (3-Day Timeline)

### Day 1: The "Black Box" Phase
*   **State**: We had a treadmill that only worked with a subscription app.
*   **Action**: Recorded Bluetooth traffic while pressing "Start", "Speed Up", and "Stop" on the official app.
*   **Analysis**: The AI analyzed `.pklg` files and hypothesized that the protocol used a "Chunked" format. We identified the checksum algorithm (Simple Sum Mod 256).

### Day 2: The "Handshake" Breakthrough
*   **Blocker**: We could send speed commands, but the treadmill ignored them or disconnected immediately.
*   **Discovery**: Comparing "Success" logs vs "Failure" logs, the AI isolated a specific 9-step initialization sequence.
*   **Result**: We found the "Magic Packet" (Command `0x90`) that authenticates the session. Once sent, the treadmill's light turned **Blue**.

### Day 3: Refinement & FTMS Implementation
*   **Scaling**: We discovered the treadmill used unusual scaling factors:
    *   **Speed**: `Value / 100` (e.g., 400 = 4.0 kph).
    *   **Incline (Write)**: `Value / 60` (e.g., 60 = 1.0%).
    *   **Incline (Read)**: `Value / 100` (Inconsistent inputs vs outputs!).
*   **Bridge Construction**: Built the `main.py` application using `bleak` (to talk to the Treadmill) and `bless` (to talk to Zwift/Runna).
*   **ESP32 Port**: Ported the verified Python logic to C++ for a standalone dongle solution.

## 📊 Summary of Effort

| Activity            | Time Allocation                              |
| :------------------ | :------------------------------------------- |
| **Data Collection** | 10% (Capturing packets)                      |
| **Analysis (AI)**   | 40% (Parsing hex, finding patterns)          |
| **Scripting**       | 30% (Writing the Python bridge)              |
| **Debugging**       | 20% (Fixing timing issues, verified scaling) |

## 🧠 Key Learnings
1.  **Timing is Everything**: The treadmill requires commands every 500ms or it triggers a safety stop.
2.  **Proprietary Quirks**: The "Incline" command uses different math for *Reading* vs *Writing*, likely a firmware quirk of the motor controller.
3.  **AI for Reverse Engineering**: This project demonstrated that LLMs are exceptionally good at finding needles in haystacks (hex dumps) when provided with differential data (success vs fail logs).
