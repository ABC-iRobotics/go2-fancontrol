Fan control circuits for Go2 and jetson
=====================================================
Make Go2 silent - fan replacement and Fan Control Unit

## BOM

 name | part | qty | link 
 --- | --- | --- | --- 
Board | ESP32-C3-SUPERMINI | 1 | https://www.hestore.hu/prod_10048018.html 
LDO PSU | AP7375-50SA-7 | 1 | https://www.hestore.hu/prod_10049399.html
TEMP sens | DS18B20+ | 1 | https://www.hestore.hu/prod_10028985.html
PWM FET | IRLML2502 | 1 | https://www.hestore.hu/prod_10032850.html
Shottky Diode | >0.5A | 1 | Lomex 83-03-00
thermo glue | AG TG-10 | 1 | https://www.hestore.hu/prod_10037275.html
PSU input cap | 1μF ceramic | 1 |
PWM filter coil | 470uH >=0.2A, h<5mm | 1 | Lomex 93-02-85, https://www.hestore.hu/prod_10031750.html
PWM filter cap | 10μF ceramic | 2 |
1wire pullup | 4.7kOhm 5% | 1 |
Fan output conn - Go2 | JST-GH-2 male | 2 | 
Fan output conn - jetson | JST-SUR-4 male | 1 | 
power input conn from fan - Go2 | JST-GH-2 female | 1 | 
power input conn from fan - jetson | JST-SUR-4 female | 1 | 

[Go2venti](https://www.aliexpress.com/item/1005003167452424.html?spm=a2g0o.productlist.main.4.1cf97b4dZIYA20&aem_p4p_detail=202604270553073534262558789650002467519&algo_pvid=a194689f-40d1-44ba-bdc1-ed8097d2642a&algo_exp_id=a194689f-40d1-44ba-bdc1-ed8097d2642a-3&pdp_ext_f=%7B%22order%22%3A%2211%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22%7D&pdp_npi=6%40dis%21HUF%214492.26%213143.94%21%21%2114.06%219.84%21%40211b6c1917772943878626654e66da%2112000024455214574%21sea%21HU%21136054521%21X%211%210%21n_tag%3A-29919%3Bd%3A19508db5%3Bm03_new_user%3A-29895&curPageLogUid=dPjdqvEjKoIP&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A1005003167452424%7C_p_origin_prod%3A&search_p4p_id=202604270553073534262558789650002467519_1) - Blade Color: 12V Dual Ball 2.54 !  
[jetson venti](https://www.aliexpress.com/item/4001105337137.html?spm=a2g0o.productlist.main.4.7f9f5db3lMZBFu&aem_p4p_detail=202604270556077366537953709550002441223&algo_pvid=8a142dad-84c5-4ede-bf23-294db2bde029&algo_exp_id=8a142dad-84c5-4ede-bf23-294db2bde029-3&pdp_ext_f=%7B%22order%22%3A%22199%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22%7D&pdp_npi=6%40dis%21HUF%211658.24%211603.92%21%21%215.19%215.02%21%40210390b817772945675342802e167b%2112000048220772427%21sea%21HU%21136054521%21X%211%210%21n_tag%3A-29919%3Bd%3A19508db5%3Bm03_new_user%3A-29895&curPageLogUid=INIrjSIZDbQZ&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A4001105337137%7C_p_origin_prod%3A&search_p4p_id=202604270556077366537953709550002441223_1) - Size: 1PCS 4010 Blower !

## Software prequisites

https://github.com/matmunk/DS18B20
copy into arduino ide folder/libraries

## Arduino, Usage notes

Run ardiono IDE with --no-sandbox cli option.

If a stock ESP32-C3 fails to init/connect, kepp boot button pressed, while connect to USB and flash.

Enable USB CDC On boot to use USB serial by default & rebuild, upload

## Configuration

FAN database:  
name | start duty% | spin floor duty%
----- | -------   | -------------------
Jetson stock | 30 | 25
Ali 40x40 double ball 12V | 75 | 12


### Picking a device

Every unit advertises under the same name `go2fan`, so its BLE address is what
selects one - and stock modules can arrive carrying the same factory address,
which leaves nothing to tell two of them apart. So give each board its own:
set `BLE_MAC` in `esp32fw/ble.h` before flashing it, as `AA:BB:CC:DD:EE:FF`
(the separators are optional). Empty, the default, keeps the factory address.
Either way the address in use is printed on the boot log, so it can be read off
the serial monitor rather than guessed:

    BLE advertising started, address 10:00:3b:af:09:36

On the client side `--address` picks a unit for a single command,
`GO2FAN_ADDRESS` for a whole shell, and `DEFAULT_ADDRESS` at the top of
`ble_client.py` for good; with none given it talks to whichever unit answers
first. Case and separators do not matter, `10003baf0936` works too. All three
commands take it, so two units can be scanned, logged and commanded side by
side from two terminals.

    ./ble_client.py --address 10:00:3B:AF:09:36 write "ts"
    GO2FAN_ADDRESS=10:00:3B:AF:5D:A2 ./ble_client.py scan --log

unit | BLE MAC
--- | ---
POC | 10:00:3B:AF:5D:A3
Go2 Jetson | 10:00:3B:AF:5D:A2

### Thermal management (esp32fw/tm.h)

The fan is closed-loop controlled by a PI controller that holds the DS18B20 at
`TEMP_SETPOINT`. The integral term is what keeps it quiet: it settles on the
lowest duty that maintains the setpoint for the current load, instead of a
hand-tuned temperature-to-speed curve. Below the duty the fan can actually spin
at, a small state machine (off / start kick / running) with dwell times bridges
the gap so the fan never cycles on and off audibly. With `TM_ALWAYS_RUN` the gap
is never crossed at all: `TM_MIN_RUN_DUTY` becomes a floor the controller sits
on however cold it gets, and the fan is started on the first control step.

The setpoint and the always-on flag are runtime state, settable over BLE with
`tm sp` and `tm fon`; the macros below only give their power-on values.

The rest of this applies when the fan is *not* always on. A load light enough
that even `TM_MIN_RUN_DUTY` overcools cannot be held at the setpoint by any fan
speed, so the fan has to duty-cycle there; the wide, slow
stop band exists to keep that band as narrow as possible. A load the fan can
just about handle at its minimum makes the demand dip below the floor now and
then, and riding those dips out keeps the fan turning steadily instead of
restarting every few minutes. Simulated against a lumped thermal model, the
defaults keep everything above the minimum-speed boundary running continuously,
and cycle roughly every 9 minutes below it.

 macro | default | meaning
 --- | --- | ---
`TEMP_SETPOINT` | 45.0 | degC the controller maintains at power-on, see `tm sp`
`TM_SETPOINT_MIN` / `TM_SETPOINT_MAX` | 20.0 / 90.0 | range `tm sp` accepts
`TM_ALWAYS_RUN` | 1 | power-on value of "fan always on", see `tm fon`
`SAFE_START_DUTY` | 30 | % duty the fan reliably starts spinning at
`TM_MIN_RUN_DUTY` | 25 | % lowest duty it keeps spinning at once started
`TM_KP` | 6.0 | proportional gain, %/degC
`TM_KI` | 0.30 | integral gain, %/(degC*s) - halve it if the speed hunts
`TM_SAMPLE_MS` | 1000 | control period, also the sensor sample rate
`TM_FILTER_ALPHA` | 0.3 | EMA weight of a new reading (the sensor runs at 9 bits, 0.5degC steps)
`TM_KICK_MS` | 1500 | how long `SAFE_START_DUTY` is held after a start
`TM_STOP_HYST` | 8 | % below `TM_MIN_RUN_DUTY` the demand must fall to before stopping
`TM_STOP_DWELL_MS` | 30000 | ...and stay below for (wide and slow on purpose, see below)
`TM_MIN_ON_MS` | 15000 | minimum spin time once started
`TM_MIN_OFF_MS` | 10000 | minimum rest time once stopped
`TM_RESTART_SEED` | 0 | demand the integrator is parked at on stop, sets how long the off periods are
`TM_FAULT_DUTY` | 100 | % duty used when the sensor stops answering

## BLE commands

Written to the characteristic, the reply is read back from it, see `ble_client.py`.

 command | meaning
 --- | ---
`gf` | get fan speed in percent
`sf <0..100>` | set fan speed in percent, turns thermal management off
`tm <0\|1>` | thermal management off/on, takes over the fan without a speed jump
`tm en <0\|1>` | same, spelled out
`tm fon <0\|1>` | fan always on: 1 floors the fan at `TM_MIN_RUN_DUTY` and starts it on the next control step, 0 lets the stop band shut it down again once the dwell times have passed
`tm sp <degC>` | temperature setpoint, 20..90, fractions allowed (`tm sp 42.5`)
`ts` | thermal status: state, temperature, setpoint, always-on, P, I, demand, fan

## Logging

The advertised heatsink temperature and fan speed can be recorded, one
datapoint per second, to a file or watched live in Foxglove. Both flags take
the same samples and can be given together; they land on the `/go2fan` topic as
`temperature_c`, `fan_percent` and `rssi`, with the BLE name and address
attached as metadata.

 flag | needs | what it does
 --- | --- | ---
`scan --log [FILE]` | `pip install mcap` | writes an MCAP file Foxglove opens directly; without FILE the name is built from the device name, MAC and the date
`scan --loglive [PORT]` | `pip install foxglove-sdk` | serves the samples live over a WebSocket on PORT (default 8765); the printed `app.foxglove.dev` link opens the app on it

A reading is only advertised once a second, so the extra advertisements in
between are printed but not recorded. Logged lines are marked `(logging)`.
