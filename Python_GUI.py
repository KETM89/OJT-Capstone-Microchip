import subprocess
import os
import sys
import tkinter as tk
from tkinter import messagebox

BACKEND_NAME = "Prototype1.exe"  

def get_backend_path():
   """
   Look for the backend EXE in the same folder as this GUI.
   - When running as a script: use this .py file's folder
   - When frozen with PyInstaller: use sys.executable's folder
   """
   if getattr(sys, 'frozen', False):
       base_dir = os.path.dirname(sys.executable)
   else:
       base_dir = os.path.dirname(os.path.abspath(__file__))
   return os.path.join(base_dir, BACKEND_NAME)

# Globals for session and countdown
current_proc = None
session_seconds_left = 0
session_running = False

def start_session(mode, label):
   """
   Start a shift session:
     mode  = string passed to backend (e.g. "phone", "on", "break")
     label = user-friendly label shown in GUI (e.g. "Phone shift")
   """
   global current_proc, session_seconds_left, session_running
   # Read minutes from entry
   try:
       minutes = int(minutes_var.get())
   except ValueError:
       messagebox.showerror("Error", "Please enter a valid integer for minutes.")
       return
   if minutes <= 0:
       messagebox.showerror("Error", "Minutes must be greater than 0.")
       return
   exe_path = get_backend_path()
   if not os.path.isfile(exe_path):
       messagebox.showerror(
           "Backend not found",
           f"{BACKEND_NAME} was not found in:\n{os.path.dirname(exe_path)}\n\n"
           "For final setup in the lab:\n"
           f"→ Place {BACKEND_NAME} in the same folder as this GUI exe."
       )
       return
   # If something is already running, ask user to stop first
   if current_proc is not None and current_proc.poll() is None:
       messagebox.showinfo("Info", "A session is already running.\nPress Stop first.")
       return
   # Start backend for this mode
   cmd = [exe_path, mode, str(minutes)]
   try:
       current_proc = subprocess.Popen(cmd)
   except Exception as e:
       messagebox.showerror("Error", f"Failed to start backend EXE:\n{e}")
       current_proc = None
       return
   # Start countdown
   session_seconds_left = minutes * 60
   session_running = True
   status_var.set(f"{label} running for {minutes} min...")
   update_countdown()

def stop_session():
   global current_proc, session_running, session_seconds_left
   exe_path = get_backend_path()
   # 1) If a session is running, terminate it
   if current_proc is not None and current_proc.poll() is None:
       try:
           current_proc.terminate()
           current_proc.wait(timeout=2)
       except Exception:
           pass
   current_proc = None
   session_running = False
   session_seconds_left = 0
   countdown_var.set("--:--")
   status_var.set("Session stopped.")
   # 2) Show "Session Stopped" on LCD via backend stop mode
   if os.path.isfile(exe_path):
       try:
           # fire-and-forget; short process just updates LCD + 7-seg
           subprocess.Popen([exe_path, "stop"])
       except Exception:
           # If this fails, we silently ignore and just keep GUI state
           pass

def check_process_finished():
   """
   Check if backend exited by itself (e.g. timer done on C++ side).
   Runs every 500 ms.
   """
   global current_proc, session_running, session_seconds_left
   if current_proc is not None and current_proc.poll() is not None:
       # Process is done
       current_proc = None
       if session_running:
           session_running = False
           session_seconds_left = 0
           countdown_var.set("00:00")
           status_var.set("Session finished.")
   root.after(500, check_process_finished)

def seconds_to_mmss(sec):
   m = sec // 60
   s = sec % 60
   return f"{m:02d}:{s:02d}"

def update_countdown():
   """
   Update the countdown label once per second while session_running.
   """
   global session_seconds_left, session_running
   if session_running and session_seconds_left > 0:
       countdown_var.set(seconds_to_mmss(session_seconds_left))
       session_seconds_left -= 1
       root.after(1000, update_countdown)
   elif session_running and session_seconds_left <= 0:
       session_running = False
       countdown_var.set("00:00")
       status_var.set("Time elapsed (waiting for backend to finish).")

# GUI SETUP
root = tk.Tk()
root.title("Shift Timer â€” USB7002")
minutes_var = tk.StringVar(value="30")        # default minutes
status_var = tk.StringVar(value="Ready.")
countdown_var = tk.StringVar(value="--:--")
# Title
tk.Label(root, text="USB7002 Shift Timer", font=("Segoe UI", 14, "bold")).pack(pady=10)
# Minutes input
frame_top = tk.Frame(root)
frame_top.pack(pady=5)
tk.Label(frame_top, text="Minutes per session:").pack(side=tk.LEFT, padx=5)
tk.Entry(frame_top, textvariable=minutes_var, width=6).pack(side=tk.LEFT)
tk.Label(frame_top, text="(change before starting)").pack(side=tk.LEFT, padx=5)
# Mode buttons
frame_buttons = tk.Frame(root)
frame_buttons.pack(pady=10)
tk.Button(frame_buttons, text="Phone shift",
         command=lambda: start_session("phone", "Phone shift"),
         width=15).grid(row=0, column=0, padx=5)
tk.Button(frame_buttons, text="On-Chat/Call",
         command=lambda: start_session("onchat", "On-Chat/Call"),
         width=15).grid(row=0, column=1, padx=5)
tk.Button(frame_buttons, text="Break",
         command=lambda: start_session("break", "Break"),
         width=15).grid(row=0, column=2, padx=5)
# Stop / Exit buttons
frame_bottom = tk.Frame(root)
frame_bottom.pack(pady=10)
tk.Button(frame_bottom, text="Stop",
         command=stop_session, width=10, bg="red", fg="white").pack(side=tk.LEFT, padx=5)
tk.Button(frame_bottom, text="Exit",
         command=root.destroy, width=10).pack(side=tk.LEFT, padx=5)
# Countdown + status
tk.Label(root, text="Time remaining:", font=("Segoe UI", 10, "bold")).pack(pady=(5, 0))
tk.Label(root, textvariable=countdown_var,
        font=("Consolas", 18, "bold")).pack(pady=(0, 5))
tk.Label(root, textvariable=status_var, width=50).pack(pady=5)
# Periodic checker for process completion
root.after(500, check_process_finished)
root.mainloop()
