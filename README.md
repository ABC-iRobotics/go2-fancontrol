Fan control circuits for Go2 and jetson
=====================================================
Make Go2 silent - fan replacement and Fan Control Unit

## BOM - Go2 mainboard set

 name | part | qty | link 
 --- | --- | --- | --- 
Board | ESP32-C3-SUPERMINI | 1 | https://www.hestore.hu/prod_10048018.html 
LDO PSU | AP7375-50SA-7 | 1 | https://www.hestore.hu/prod_10049399.html
TEMP sens | DS18B20+ | 1 | https://www.hestore.hu/prod_10028985.html
PWM FET | IRLML2502 | 1 | https://www.hestore.hu/prod_10032850.html
Flyback Diode | 1N4001 | 1 | https://www.hestore.hu/prod_10037723.html
thermo glue | AG TG-10 | 1 | https://www.hestore.hu/prod_10037275.html
PSU input cap | 1μF ceramic | 1 |
PWM filter LF | 47μF electrolytic | 1 |
PWM filter HF | 4.7μF ceramic | 1 |
1wire pullup | 4.7kOhm 5% | 1 |

[Go2venti](https://www.aliexpress.com/item/1005003167452424.html?spm=a2g0o.productlist.main.4.1cf97b4dZIYA20&aem_p4p_detail=202604270553073534262558789650002467519&algo_pvid=a194689f-40d1-44ba-bdc1-ed8097d2642a&algo_exp_id=a194689f-40d1-44ba-bdc1-ed8097d2642a-3&pdp_ext_f=%7B%22order%22%3A%2211%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22%7D&pdp_npi=6%40dis%21HUF%214492.26%213143.94%21%21%2114.06%219.84%21%40211b6c1917772943878626654e66da%2112000024455214574%21sea%21HU%21136054521%21X%211%210%21n_tag%3A-29919%3Bd%3A19508db5%3Bm03_new_user%3A-29895&curPageLogUid=dPjdqvEjKoIP&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A1005003167452424%7C_p_origin_prod%3A&search_p4p_id=202604270553073534262558789650002467519_1) - Blade Color: 12V Dual Ball 2.54 !  
[jetson venti](https://www.aliexpress.com/item/4001105337137.html?spm=a2g0o.productlist.main.4.7f9f5db3lMZBFu&aem_p4p_detail=202604270556077366537953709550002441223&algo_pvid=8a142dad-84c5-4ede-bf23-294db2bde029&algo_exp_id=8a142dad-84c5-4ede-bf23-294db2bde029-3&pdp_ext_f=%7B%22order%22%3A%22199%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22%7D&pdp_npi=6%40dis%21HUF%211658.24%211603.92%21%21%215.19%215.02%21%40210390b817772945675342802e167b%2112000048220772427%21sea%21HU%21136054521%21X%211%210%21n_tag%3A-29919%3Bd%3A19508db5%3Bm03_new_user%3A-29895&curPageLogUid=INIrjSIZDbQZ&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A4001105337137%7C_p_origin_prod%3A&search_p4p_id=202604270556077366537953709550002441223_1) - Size: 1PCS 4010 Blower !

## Software prequisites

https://github.com/matmunk/DS18B20

jetson fan header: W: 3.96mm, mélység: 1.3mm, raster: 0.8mm
JST SUR 4pin

## Arduino, Usage notes

Run ardiono IDE with --no-sandbox cli option.

Enable USB CDC On boot to use USB serial by default & rebuild, upload

BLE device MAC: 10:00:3B:AF:09:36
