Import("env")

# esptool 5.x uses Unicode progress bars. Force UTF-8 for child processes so
# uploads work from Windows installations whose default console is cp1252.
env["ENV"]["PYTHONUTF8"] = "1"
env["ENV"]["PYTHONIOENCODING"] = "utf-8"

