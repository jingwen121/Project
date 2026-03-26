/* Start Header
*****************************************************************/
/*!
\file README.txt
\author Liu Jingwen l.jingwen@digipen.edu
\date 15 Mar 2026
\brief
README for Assignment 4 - Reliable FTP over UDP
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

===========================================================
  CSD2161 / CSD2160 - Assignment 4: Reliable FTP over UDP
===========================================================

-----------
HOW TO BUILD
-----------
1. Open the Visual Studio solution for the server:
     ass4\server\server.sln

2. Open the Visual Studio solution for the client:
     ass4\client\client.sln

3. Set both projects to Release mode (x64).

4. Build Solution for each (Ctrl+Shift+B).

Executables will be at:
   ass4\server\x64\Release\server.exe
   ass4\client\x64\Release\client.exe

-----------
HOW TO RUN
-----------

STEP 1 - Place config.txt in the SAME folder as server.exe
         (e.g. ass4\server\x64\Release\config.txt)

STEP 2 - Open a Command Prompt and run the server:
   > cd ass4\server\x64\Release
   > server.exe

   The server will prompt:
     Server TCP port Number : 8000
     Server UDP Port Number : 9000
     Path                   : C:\Network_Assignments\ass4\FTP_Files

STEP 3 - Open another Command Prompt and run the client:
   > cd ass4\client\x64\Release
   > client.exe

   The client will prompt:
     Server IP Address      : 192.168.1.20   (use server's actual LAN IP)
     Server TCP Port Number : 8000
     Server UDP Port Number : 9000
     Client UDP Port Number : 9010
     Path                   : C:\Network_Assignments\ass4\client\Downloaded

   NOTE: Make sure the Path (download destination) folder exists before running.

STEP 4 - Type commands in the client window:
   /l                              - list available files on server
   /d <clientIP>:<udpPort> <file>  - download a file via UDP
   /q                              - quit

-----------
CONFIG FILE (config.txt)
-----------
Place config.txt next to server.exe. Format:

   # Lines starting with # are comments
   UDP_CHUNK_SIZE=61440      # bytes per UDP datagram (default 60KB)
   UDP_TIMEOUT_MS=500        # ms to wait for ACK before retransmit
   UDP_MAX_RETRIES=20        # max retransmits before giving up

If config.txt is not found, default values are used automatically.

-----------
PROTOCOL SUMMARY
-----------
- Control messages use TCP (list, download request, quit).
- File data is transferred over UDP using Stop-and-Wait:
    1. Server sends one chunk (up to UDP_CHUNK_SIZE bytes).
    2. Client sends ACK back to the server sender port.
    3. Server retransmits if no ACK within UDP_TIMEOUT_MS.
    4. Each download session uses its own UDP socket so ACKs
       from concurrent sessions are never mixed up.
- UDP datagram fields:
    SessionID(4) | FileLen(4) | FileOffset(4) | DataLen(4) |
    Flags(1) | SeqNum(4) | FileData(variable)
  Flags: 0x00 = data packet, 0x01 = ACK packet

-----------
TEST CASES
-----------
Replace <ip> with the client's actual LAN IP (e.g. 192.168.1.20)
Replace <port> with the client's UDP port (e.g. 9010)

--- Basic Tests (1 client) ---

Test 1 - REQ_LISTFILES:
  Command : /l
  Expected: List of all files in server FTP_Files folder printed.

Test 2 - Download T1.txt (0 bytes):
  Command : /d <ip>:<port> T1.txt
  Expected: T1.txt created in Downloaded folder, file size = 0 bytes.

Test 3 - Download T2.txt (1 byte):
  Command : /d <ip>:<port> T2.txt
  Expected: T2.txt in Downloaded folder, file size = 1 byte.

Test 4 - Download File_000.txt:
  Command : /d <ip>:<port> File_000.txt
  Expected: File_000.txt downloaded correctly.

Test 5 - Download File_001.txt:
  Command : /d <ip>:<port> File_001.txt
  Expected: File_001.txt downloaded correctly.

Test 6 - Download File_002.txt:
  Command : /d <ip>:<port> File_002.txt
  Expected: File_002.txt downloaded correctly.

Test 7 - Download File_003.txt:
  Command : /d <ip>:<port> File_003.txt
  Expected: File_003.txt downloaded correctly.

Test 8 - Download File_004.txt:
  Command : /d <ip>:<port> File_004.txt
  Expected: File_004.txt downloaded correctly.

Test 9 - Download File_005.txt:
  Command : /d <ip>:<port> File_005.txt
  Expected: File_005.txt downloaded correctly.

Test 10 - Download File_006.txt:
  Command : /d <ip>:<port> File_006.txt
  Expected: File_006.txt downloaded correctly.

Test 11 - Download File_007.txt:
  Command : /d <ip>:<port> File_007.txt
  Expected: File_007.txt downloaded correctly.

Test 12 - Download File_008.txt:
  Command : /d <ip>:<port> File_008.txt
  Expected: File_008.txt downloaded correctly.

Test 13 - Download File_009.txt:
  Command : /d <ip>:<port> File_009.txt
  Expected: File_009.txt downloaded correctly.

Test 14 - Download file with space in name:
  Command : /d <ip>:<port> File_100_Name Space.txt
  Expected: File_100_Name Space.txt downloaded correctly.

Test 15 - Download T3.mp3 (65.8 MB):
  Command : /d <ip>:<port> T3.mp3
  Expected: T3.mp3 downloaded, size = 69,089,845 bytes.
            File content matches original (compare MD5 or file size).

Test 16 - Download T5.mp3 (176 MB):
  Command : /d <ip>:<port> T5.mp3
  Expected: T5.mp3 downloaded, size = 185,323,520 bytes.

Test 17 - Download T5.mp3 with Ethernet Break Circuit:
  Command : /d <ip>:<port> T5.mp3
  Steps   : While download is in progress, briefly press the Ethernet
            break circuit button (< 0.5 sec) one or more times.
  Expected: Download continues and completes correctly despite
            temporary disconnections. Server retransmits lost packets.

--- Concurrent Tests (2 clients) ---

Test 18 - CD1_Diff_File (two clients, different files):
  Setup   : Run two client.exe instances with different UDP ports
            (e.g. Client A = port 9010, Client B = port 9011).
  Client A: /d <ip>:9010 T3.mp3
  Client B: /d <ip>:9011 T4.mp3
  Expected: Both files downloaded correctly and simultaneously.

Test 19 - CD2_Same_File (two clients, same file):
  Setup   : Run two client.exe instances.
  Client A: /d <ip>:9010 T3.mp3
  Client B: /d <ip>:9011 T3.mp3
  Expected: Both clients receive T3.mp3 correctly simultaneously.

--- Error Recovery Tests ---

Test 20 - E1_Idle (client crash while idle):
  Steps   : 1. Connect client to server (no commands sent).
            2. Close the client CMD window forcefully (without /q).
            3. Start a new client and connect again.
  Command : /d <ip>:<port> File_000.txt
  Expected: Server handles the abrupt disconnect gracefully.
            New client can download File_000.txt successfully.

Test 21 - E2_Download (client crash during download):
  Steps   : 1. Start downloading T5.mp3.
            2. While downloading, close the client CMD window forcefully.
            3. Start a new client and connect again.
  Command : /d <ip>:<port> File_000.txt
  Expected: Server detects the lost connection and cleans up the session.
            New client can download File_000.txt successfully.

Test 22 - E3_Concurrent (one client crashes during concurrent download):
  Steps   : 1. Run Client A and Client B concurrently.
            2. Client A: /d <ip>:9010 T3.mp3
               Client B: /d <ip>:9011 T4.mp3
            3. While both are downloading, forcefully close Client A.
  Expected: Client B continues and completes T4.mp3 without interruption.
            Start Client A again; it can download File_000.txt successfully.

--- Error Handling ---

Test 23 - Bad request (non-existing file):
  Command : /d <ip>:<port> File_999.txt
  Expected: Server sends DOWNLOAD_ERROR. Client prints error message.
            No file is created in the Downloaded folder.

Test 24 - Quit:
  Command : /q
  Expected: Client sends REQ_QUIT, disconnects cleanly from server.
            Server continues running and accepts new clients.

