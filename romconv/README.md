# romconv - ROM/bin file conversion

Arcade emulation involves dealing with the original ROM files of the
machines in question. Sometimes the conversion is little more than the
transcription from binary to a equivalent C source file.  But in many
cases this conversion includes data processing. E.g.  all color tables
are converted into the 16 bit color format used by the ILI9341 or
ST7789 displays. Sprite and tile data is converted into a format
easier to process on the ESP32.

This could all be done on the ESP32 target at run time. But here it's
done beforehand offloading these tasks from he EPS32.

It's possible to implement only one or two of the three arcade
machines.  In that case the related ROM conversion can be omitted and
the machine in question has to be disabled in the file
[config.h](../galagino/config.h).

The necessary ROM files need be pleced in the [roms directory](../roms)
before these scripts can be run. Once these are converted you need
also also (convert the audio samples)[../samples].

## Generic ROM conversion

```
./tileaddr.py ../galagino/tileaddr.h
./z80patch.py
```

## Pac-Man ROM conversion

```
./audioconv.py pacman_wavetable ../roms/82s126.1m ../roms/82s126.3m ../galagino/pacman_wavetable.h
./cmapconv.py pacman_colormap ../roms/82s123.7f 0 ../roms/82s126.4a ../galagino/pacman_cmap.h
./logoconv.py ../logos/pacman.png ../galagino/pacman_logo.h
./romconv.py pacman_rom ../roms/pacman.6e ../roms/pacman.6f ../roms/pacman.6h ../roms/pacman.6j ../galagino/pacman_rom.h
./spriteconv.py pacman_sprites pacman ../roms/pacman.5f ../galagino/pacman_spritemap.h
./tileconv.py ../roms/pacman.5e ../galagino/pacman_tilemap.h
```

## Galaga ROM conversion

```
./audioconv.py galaga_wavetable ../roms/prom-1.1d ../galagino/galaga_wavetable.h
./cmapconv.py galaga_colormap_sprites ../roms/prom-5.5n 0 ../roms/prom-3.1c ../galagino/galaga_cmap_sprites.h
./cmapconv.py galaga_colormap_tiles ../roms/prom-5.5n 16 ../roms/prom-4.2n ../galagino/galaga_cmap_tiles.h
./logoconv.py ../logos/galaga.png ../galagino/galaga_logo.h
./romconv.py galaga_rom_cpu1 ../roms/gg1_1b.3p ../roms/gg1_2b.3m ../roms/gg1_3.2m ../roms/gg1_4b.2l ../galagino/galaga_rom1.h
./romconv.py galaga_rom_cpu2 ../roms/gg1_5b.3f ../galagino/galaga_rom2.h
./romconv.py galaga_rom_cpu3 ../roms/gg1_7b.2c ../galagino/galaga_rom3.h
./spriteconv.py galaga_sprites galaga ../roms/gg1_11.4d ../roms/gg1_10.4f ../galagino/galaga_spritemap.h
./starsets.py ../galagino/galaga_starseed.h
./tileconv.py ../roms/gg1_9.4l ../galagino/galaga_tilemap.h
```

## Donkey Kong ROM conversion

```
./cmapconv.py dkong_colormap ../roms/c-2k.bpr ../roms/c-2j.bpr 0 ../roms/v-5e.bpr ../galagino/dkong_cmap.h
./logoconv.py ../logos/dkong.png ../galagino/dkong_logo.h
./romconv.py dkong_rom ../roms/c_5et_g.bin ../roms/c_5ct_g.bin ../roms/c_5bt_g.bin ../roms/c_5at_g.bin ../galagino/dkong_rom.h
./spriteconv.py dkong_sprites dkong ../roms/l_4m_b.bin  ../roms/l_4n_b.bin  ../roms/l_4r_b.bin  ../roms/l_4s_b.bin../galagino/dkong_spritemap.h  
./tileconv.py ../roms/v_5h_b.bin ../roms/v_3pt.bin ../galagino/dkong_tilemap.h
```