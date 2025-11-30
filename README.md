# ESP32 Proxmox monitoring system

Model used: TTGO T-Display Standard with 240x135 OLED display

# Functions:
- Automatically connects to Wi-Fi and fetches data from Proxmox via API
- Converts data to apprehensible formats
- Displays a wallpaper of your liking (RGB565 format)
- Splits data into 3 pages by default and cycles through them automatically/via button press
- Displays a status bar with current server status, page name and page number on the top

# Pages
# 1. RESOURCES
Displays CPU/RAM/Disk/Swap usage as bars and miscellaneous below each of them (node name, overall ram/disk space)

# 2. STATUS 
Displays uptime. network activity, 1m/5m/15m load, data totals

# 3. GUESTS
Displays total LXC and VM count
