# How to build

```
git clone https://github.com/nopnop2002/esp-idf-irSend
cd esp-idf-irSend/esp-idf-irSend-StickC+2/
idf.py set-target esp32
idf.py menuconfig
idf.py flash -b 115200 monitor
```

\*There is no MENU ITEM where this application is peculiar.   

__You need to specify Baud rate for flashing.__   

---

# How to use

Select IR code by ButtonB (Side Button) press.   
When a ButtonB is pressed for more than 2 seconds, it show initial screen.   
Fire IR code by ButtonA (Front Button) press.   

![Image](https://github.com/user-attachments/assets/001a8f88-fbd7-49df-98c5-d37bd4e784c9)
