#!/bin/bash

# Ensure script is run as root
if [ "$EUID" -ne 0 ]; then 
  echo "Please run as root (sudo)"
  exit
fi

echo "Stopping thermal daemon to prevent overrides..."
systemctl stop nvfancontrol

# Iterate through all available pwmchips (usually 0 through 4 or 8)
for chip in /sys/class/pwm/pwmchip*; do
    chip_name=$(basename "$chip")
    echo "------------------------------------------"
    echo "Processing $chip_name..."

    # Check if pwm0 is already exported, if not, export it
    if [ ! -d "$chip/pwm0" ]; then
        echo "Exporting channel 0 on $chip_name..."
        echo 0 > "$chip/export" 2>/dev/null
    fi

    # Verify export success
    if [ -d "$chip/pwm0" ]; then
        echo "Configuring $chip_name/pwm0..."
        
        # Disable first to change period safely
        echo 0 > "$chip/pwm0/enable"
        
        # Set period to 40000 ns (25kHz)
        # Some chips have hardware restrictions on period; 
        # we use 2>/dev/null to skip chips that don't support this specific frequency.
        echo 40000 > "$chip/pwm0/period" 2>/dev/null
        
        # Set duty_cycle to 0 (Target: OFF)
        echo 0 > "$chip/pwm0/duty_cycle"
        
        # Enable the output
        echo 1 > "$chip/pwm0/enable"
        
        echo "Applied 0% duty cycle to $chip_name."
    else
        echo "Could not export channel 0 on $chip_name (likely in use or restricted)."
    fi
    sleep 2
done

echo "------------------------------------------"
echo "Script complete."
