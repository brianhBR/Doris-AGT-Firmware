# Flashing SparkFun Artemis Global Tracker Production Firmware

This guide covers how to flash the **latest SparkFun firmware** onto the Artemis Global Tracker (AGT). The AGT in this repo runs **Doris firmware** (drop-camera comms hub). If you need to run SparkFun’s stock firmware instead (e.g. full Global Tracker or production test), use one of the methods below.

## Which firmware?

| Firmware | Use case | Source in this repo |
|----------|----------|----------------------|
| **Example16 – Global Tracker** | Full SparkFun tracker (Iridium, GPS, config tool) | `SparkFun_Artemis_Global_Tracker/Software/examples/Example16_GlobalTracker/` |
| **Example17 – Production Test** | Factory test (needs AGT Test Header hardware) | `SparkFun_Artemis_Global_Tracker/Software/examples/Example17_ProductionTest/` |

“Production firmware” usually means **Example16** (what ships on the board). **Example17** is for production *testing* with the test header.

---

## Option A: Artemis Firmware Upload GUI (easiest if you have a .bin)

1. **Get a .bin file**
   - Check [SparkFun AGT releases](https://github.com/sparkfun/Artemis_Global_Tracker/releases) for pre-built binaries (e.g. Example16), or
   - Build in Arduino IDE (Option B) and use “Export compiled Binary” (Sketch → Export compiled Binary), then find the `.bin` in the sketch folder.

2. **Get the uploader**
   - Download [Artemis Firmware Upload GUI](https://github.com/sparkfun/Artemis-Firmware-Upload-GUI/releases) (e.g. Windows .exe under Assets).

3. **Flash**
   - Connect the AGT via **USB**.
   - Run the GUI → select the **.bin** file → choose the **COM port** → click **Upload**.

---

## Option B: Arduino IDE (build and upload or export .bin)

1. **Board support**
   - Install **SparkFun Apollo3** core **v2.1.0** (required for AGT examples).
   - In Board Manager, select **“RedBoard Artemis ATP”** (not “Artemis Module”).

2. **Libraries** (Library Manager)
   - **IridiumSBDi2c** (e.g. v3.0.5 or v3.0.6)
   - **SparkFun u-blox GNSS**
   - **SparkFun PHT MS8607** (for Example16)

3. **Open the example**
   - **Example16:**  
     `SparkFun_Artemis_Global_Tracker/Software/examples/Example16_GlobalTracker/Example16_GlobalTracker.ino`
   - **Example17:**  
     `SparkFun_Artemis_Global_Tracker/Software/examples/Example17_ProductionTest/Example17_ProductionTest.ino`  
     (Example17 needs the [AGT Test Header](https://www.sparkfun.com/products/18712) connected.)

4. **Upload**
   - Select port (e.g. COM7).
   - **Sketch → Upload**.

   To get a .bin for the GUI: **Sketch → Export compiled Binary**, then use that .bin in Option A.

---

## Option C: PlatformIO (this repo builds Doris only)

This repo’s **PlatformIO** setup builds and flashes **Doris firmware** only. It does not build Example16/Example17. To flash SparkFun production firmware, use Option A or B above.

To flash **Doris** with PlatformIO (once `pio` is in PATH):

```bash
pio run -t upload
```

---

## After flashing SparkFun firmware

- **Example16:** Use the [Artemis Global Tracker Configuration Tool (AGTCT)](https://github.com/sparkfun/Artemis_Global_Tracker/tree/master/Tools) to configure over USB or Iridium.
- **Example17:** Connect the Test Header and run the test sequence; follow prompts in the Serial Monitor (115200 baud).

To return to **Doris**, build this project and flash with PlatformIO (`pio run -t upload`) or Arduino IDE using the Doris source.
