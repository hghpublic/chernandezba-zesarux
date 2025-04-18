/*
    ZEsarUX  ZX Second-Emulator And Released for UniX
    Copyright (C) 2013 Cesar Hernandez Bano

    This file is part of ZEsarUX.

    ZEsarUX is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <string.h>

#include "pcw.h"
#include "cpu.h"
#include "screen.h"
#include "debug.h"
#include "contend.h"
#include "joystick.h"
#include "zxvision.h"
#include "operaciones.h"
#include "utils.h"
#include "audio.h"

#include "dsk.h"
#include "pd765.h"
#include "ula.h"
#include "ay38912.h"
#include "settings.h"

/*

Info PCW:

Información pcw

Amstrad PCW

Boot

PCW boot sequence

Boot mister

Amstrad-PCW_MiSTer/boot_loader.sv at master · MiSTer-devel/Amstrad-PCW_MiSTer · GitHub

Emulador

JOYCE for UNIX

https://www.habisoft.com/pcwwiki/doku.php?id=en:sistema:indice

Más hardware

https://www.seasip.info/Unix/Joyce/hardware.pdf
￼


*/

//Direcciones donde estan cada pagina de ram. 128 paginas de 16 kb cada una
//asignamos 2 MB para tener el máximo de memoria ya disponible
z80_byte *pcw_ram_mem_table[PCW_MAX_RAM_PAGES];





//Direcciones actuales mapeadas para lectura, bloques de 16 kb
z80_byte *pcw_memory_paged_read[4];

//Direcciones actuales mapeadas para lectura, bloques de 16 kb
z80_byte *pcw_memory_paged_write[4];


//Paginas mapeadas para lectura en los 4 segmentos, usado para lectura de teclado y en debug
z80_byte pcw_banks_paged_read[4];

//Contador de linea para lanzar interrupcion.
z80_byte pcw_scanline_counter;

z80_bit pcw_pending_interrupt={0};


z80_byte pcw_keyboard_table[16]={
    255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255
};

z80_byte pcw_interrupt_from_pd765_type=0;

//Total ram. Para un 8256, 256k. Para un 8512, 512k
int pcw_total_ram=256*1024;


//Determina la paleta del mode1 (la que es como cga)
int pcw_mode1_palette=0;

//Mostrar colores en blanco y negro
z80_bit pcw_black_white_display={0};


//Siempre ver la pantalla aunque bit de F7 diga lo contrario
z80_bit pcw_always_on_display={0};

//No dejar que se invierta el color
z80_bit pcw_do_not_inverse_display={0};

//No dejar usar scroll
z80_bit pcw_do_not_scroll={0};

//
// Inicio de variables necesarias para preservar el estado (o sea las que tienen que ir en un snapshot)
//

//Registros de bancos para F0,F1,F2,F3
z80_byte pcw_bank_registers[4];

//Port F4 If a memory range is set as “locked”, then the “block to read” bits are ignored; memory is read from the “block to write”.
//Bit 7: lock C000..FFFF
//Bit 6: lock 8000..BFFF
//Bit 5: lock 4000..7FFF
//Bit 4: lock 0..3FFF
//Bits 3-0: unused
z80_byte pcw_port_f4_value;

//Address of roller RAM. b7-5: bank (0-7). b4-1: address / 512.
z80_byte pcw_port_f5_value;

//Vertical screen position
z80_byte pcw_port_f6_value;

//b7: reverse video. b6: screen enable.
z80_byte pcw_port_f7_value;

z80_byte pcw_port_f8_value;

//z80_byte pcw_interrupt_counter;

//
// Fin de variables necesarias para preservar el estado (o sea las que tienen que ir en un snapshot)
//

//Cambio de modos de video en los modelos ficticios Plus inventados por habisoft
//Out puerto 80h,0. 81h, modo (0,1,2,3)
//Out puerto 80h,20h+indice. 81h, color R,G,B (24 bits) en esa paleta. incrementando indice a cada escritura (cuando haya escrito los 3 RGB)
z80_bit pcw_allow_videomode_change={1};

z80_byte pcw_last_port_80_value=0;
z80_byte pcw_last_port_81_value=0;
int pcw_last_index_color_change=0;
int pcw_last_index_color_change_component=0;

z80_byte *pcw_get_memory_offset_read(z80_int dir)
{
    int segmento=dir/16384;

    int offset=dir & 16383;

    z80_byte *puntero;

    puntero=pcw_memory_paged_read[segmento];

    puntero +=offset;

    return puntero;
}

z80_byte *pcw_get_memory_offset_write(z80_int dir)
{
    int segmento=dir/16384;

    int offset=dir & 16383;

    z80_byte *puntero;

    puntero=pcw_memory_paged_write[segmento];

    puntero +=offset;

    return puntero;
}

z80_byte pcw_get_mask_bank_ram(void)
{
    //Si pcw_total_ram=256*1024, mascara 15
    //Si pcw_total_ram=512*1024, mascara 31

    int max_banks=pcw_total_ram/16384;

    return max_banks-1;
}

void pcw_set_memory_pages(void)
{


    z80_byte bank;

    int i;

    //z80_byte port_f4_mask=0x10;

    for (i=0;i<4;i++) {

        //Sending the bank number (with b7 set) to one of ports &F0-&F3 selects that bank for reading and writing.
        //Sending the bank number for writing to b0-2 of a port and the bank for reading to b4-b6 (with b7 reset)
        //maps separate banks in for reading and writing: this can only be used for the first 8 banks.

        bank=pcw_bank_registers[i];

        //TODO. algo no he entendido bien....  gonzalezz y otros de opera
        //hacen out F3,3, cuando realmente lo que quieren hacer es paginar en el modo extendido (sin tener el bit 7 alzado)
        //Con eso cargan los de opera, batman y demas...
        //bank |=128;


        if (bank & 128) {
            //PCW (“extended”) paging mode
            bank &=pcw_get_mask_bank_ram();
            //printf("mask %d\n",pcw_get_mask_bank_ram());
            pcw_memory_paged_read[i]=pcw_ram_mem_table[bank];
            pcw_memory_paged_write[i]=pcw_ram_mem_table[bank];

            pcw_banks_paged_read[i]=bank;
        }
        else {
            //CPC (“standard”) paging mode
            //Juegos de Opera Soft, Batman etc usan este modo
            z80_byte bank_write=bank & 7;
            z80_byte bank_read=(bank >> 4) & 7;

            //Port F4: If a memory range is set as “locked”, then the “block to read” bits are ignored; memory is read from the “block to write”.
            /*
                    D7              RW3  (CPU bank C000h to FFFFh)
                    D6              RW0  (CPU bank 0000h to 3FFFh)
                    D5              RW2  (CPU hank B000h to BFFFh)
	                D4		RW1  (CPU bank 4000h to 7FFFh)
            */
            z80_byte port_f4_mask;
            switch(i) {
                case 0:
                    port_f4_mask=0x40;
                break;

                case 1:
                    port_f4_mask=0x10;
                break;

                case 2:
                    port_f4_mask=0x20;
                break;

                default:
                    port_f4_mask=0x80;
                break;
            }
            if (pcw_port_f4_value & port_f4_mask) {
                //printf("memory locked\n");
                //sleep(2);
                bank_read=bank_write;
            }

            pcw_memory_paged_read[i]=pcw_ram_mem_table[bank_read];
            pcw_memory_paged_write[i]=pcw_ram_mem_table[bank_write];

            pcw_banks_paged_read[i]=bank_read;

        }

        //port_f4_mask=port_f4_mask<<1;

    }


}

//Tabla de colores en rgb24, solo se usa al principio para convertirla a rgb15
int pcw_rgb_table[PCW_TOTAL_PALETTE_COLOURS]={

    0x000000, //negro
    0x41FF00,  //verde. Tipico color de green "P1" phosphor

    //4 colores tipicos de CGA

    //Paleta 0 low intensity
    //negro, green, red, brown
    0x000000, //negro
    0x00AA00, //green
    0xAA0000, //red
    0xAA5500, //brown

    //Paleta 0 high intensity
    //negro, light green, light red, yellow
    0x000000, //negro
    0x55FF55, //light green,
    0xFF5555, //light red
    0xFFFF55, //yellow

    //Paleta 1 low intensity
    //negro, cyan, magenta, light gray
    0x000000, //negro
    0x00AAAA, //cyan
    0xAA00AA, //magenta
    0xAAAAAA, //light gray

    //Paleta 1 high intensity
    0x000000, //negro
    0x55FFFF, //light cyan
    0xFF55FF, //light magenta
    0xFFFFFF, //white


    //paleta 16 colores
    0x000000,
    0x0000AA,
    0x00AA00,
    0x00AAAA,
    0xAA0000,
    0xAA00AA,
    0xAA5500,
    0xAAAAAA,
    0x555555,
    0x5555FF,
    0x55FF55,
    0x55FFFF,
    0xFF5555,
    0xFF55FF,
    0xFFFF55,
    0xFFFFFF
};


//Retorna rgb15
int pcw_convert_rgb_24_to_rgb_15(int rgb24)
{
    int r8=(rgb24>>16) & 0xFF;
    int g8=(rgb24>>8) & 0xFF;
    int b8=(rgb24   ) & 0xFF;

    int r5=(r8>>3) & 0x1F;
    int g5=(g8>>3) & 0x1F;
    int b5=(b8>>3) & 0x1F;

    int rgb15=(r5<<10) | (g5<<5) | b5;

    //printf("RGB24 %06X = RGB15 %06X  R %02X G %02X B %02X ,  R %02X G %02X B %02X\n",rgb24,rgb15,r8,g8,b8,r5,g5,b5);

    return rgb15;
}

#define PCW_COLOUR_START_MODE1_RGB15 2
#define PCW_COLOUR_START_MODE2_RGB15 (PCW_COLOUR_START_MODE1_RGB15+(4*4))

//La paleta de trabajo es esta, en 15 bits, que al final se obtendran colores de paleta de tsconf de 15 bits
//Cuando el pcw modifica algun color de la paleta, se hace sobre esta paleta de 15 bits
int pcw_rgb_table_15_bits[PCW_TOTAL_PALETTE_COLOURS];

//Convertir los colores rgb24 a rgb15
void pcw_init_colour_palette(void)
{

    int i;

    for (i=0;i<PCW_TOTAL_PALETTE_COLOURS;i++) {
        int rgb_24=pcw_rgb_table[i];
        int rgb_15=pcw_convert_rgb_24_to_rgb_15(rgb_24);
        pcw_rgb_table_15_bits[i]=rgb_15;
    }

}


//Retorna el color de 15 bits de un indice a paleta
int pcw_get_color_palette(int index)
{
    return pcw_rgb_table_15_bits[index];
}

//Retorna el color en modo 0
int pcw_get_rgb_color_mode0(int i)
{
    //0 o 1
    if (pcw_black_white_display.v) {
        return (i ? 7 : 0);
    }

    else {
        int color=pcw_get_color_palette(i);
        color +=TSCONF_INDEX_FIRST_COLOR;

        return color;
    }
}

//Retorna el color en modo 1
int pcw_get_rgb_color_mode1(int i)
{
    int indice_color=i+pcw_mode1_palette*4;
    int color_rgb15=pcw_get_color_palette(PCW_COLOUR_START_MODE1_RGB15+indice_color);

    int indice_color_rgb24=TSCONF_INDEX_FIRST_COLOR+color_rgb15;
    //printf("%d %d\n",i,indice_color_rgb24);
    return indice_color_rgb24;
}

//Retorna el color en modo 2
int pcw_get_rgb_color_mode2(int i)
{


    int indice_color=i;
    int color_rgb15=pcw_get_color_palette(PCW_COLOUR_START_MODE2_RGB15+indice_color);

    int indice_color_rgb24=TSCONF_INDEX_FIRST_COLOR+color_rgb15;
    //printf("%d %d\n",i,indice_color_rgb24);
    return indice_color_rgb24;
}

//Putpixel para modo 0
void pcw_refresca_putpixel_mode0(int x,int y,int color)
{
    y*=2;

    int color_final=pcw_get_rgb_color_mode0(color);

    scr_putpixel_zoom(x,y,color_final);
    scr_putpixel_zoom(x,y+1,color_final);
}

//Putpixel para modo 1
void pcw_refresca_putpixel_mode1(int x,int y,int color)
{
    y*=2;

    int color_final=pcw_get_rgb_color_mode1(color);

    scr_putpixel_zoom(x,y,color_final);
    scr_putpixel_zoom(x,y+1,color_final);
}

//Putpixel para modo 2
void pcw_refresca_putpixel_mode2(int x,int y,int color)
{
    y*=2;

    int color_final=pcw_get_rgb_color_mode2(color);

    scr_putpixel_zoom(x,y,color_final);
    scr_putpixel_zoom(x,y+1,color_final);
}



int pcw_get_palette_colour_indexfinal(int indice_color)
{
    int color_rgb15,color_rgb24;

    color_rgb15=pcw_get_color_palette(indice_color);
    color_rgb24=spectrum_colortable_normal[TSCONF_INDEX_FIRST_COLOR+color_rgb15];
    return color_rgb24;
}

//Retorna rgb24
int pcw_get_palette_colour(int indice_color)
{

    //printf("pcw_get_palette_colour mode %d\n",pcw_video_mode);

    switch (pcw_video_mode) {
        case 0:
            indice_color=indice_color % 2;
            return pcw_get_palette_colour_indexfinal(indice_color);
        break;

        case 1:
            indice_color=indice_color % 16;
            return pcw_get_palette_colour_indexfinal(PCW_COLOUR_START_MODE1_RGB15+indice_color);
        break;

        case 2:
        default:
            indice_color=indice_color % 16;
            return pcw_get_palette_colour_indexfinal(PCW_COLOUR_START_MODE2_RGB15+indice_color);
        break;

    }
}

void pcw_change_palette_colour_indexfinal(int indice_color,int rgb_color)
{
    int rgb15;

    rgb15=pcw_convert_rgb_24_to_rgb_15(rgb_color);
    pcw_rgb_table_15_bits[indice_color]=rgb15;

    modificado_border.v=1;

}

void pcw_change_palette_colour(int indice_color,int rgb_color)
{

    //printf("pcw_change_palette_colour mode %d\n",pcw_video_mode);


    switch (pcw_video_mode) {
        case 0:
            indice_color=indice_color % 2;
            pcw_change_palette_colour_indexfinal(indice_color,rgb_color);
        break;

        case 1:
            indice_color=indice_color % 16;
            pcw_change_palette_colour_indexfinal(PCW_COLOUR_START_MODE1_RGB15+indice_color,rgb_color);
        break;

        case 2:
        default:
            indice_color=indice_color % 16;
            pcw_change_palette_colour_indexfinal(PCW_COLOUR_START_MODE2_RGB15+indice_color,rgb_color);
        break;

    }

    //printf("indice %d color %d\n",indice_color,rgb15);

}



void pcw_reset(void)
{
    //pcw_bank_registers[0]=pcw_bank_registers[1]=pcw_bank_registers[2]=pcw_bank_registers[3]=0;

    pcw_bank_registers[0]=0x80;
    pcw_bank_registers[1]=0x81;
    pcw_bank_registers[2]=0x82;
    pcw_bank_registers[3]=0x83;

    //Importante esto en arranque, si no, no van juegos de Opera por ejemplo
    pcw_port_f4_value=0xF1;

    pcw_port_f5_value=0;
    pcw_port_f6_value=0;
    pcw_port_f7_value=0;
    pcw_port_f8_value=0;

    pcw_interrupt_from_pd765_type=0;

    //pcw_interrupt_counter=0;

    //En reset deberia activar la señal de Terminal Count, pero, tal
    //y como gestionamos esa señal, en que lanzamos una accion al recibirla, y no se mantiene una linea de señal, tal cual,
    //esto aqui no hay que lanzarlo. Ademas al hacer reset de una maquina ya se hace reset de la controladora de disco
    //pd765_set_terminal_count_signal();

    pcw_scanline_counter=0;

    pcw_pending_interrupt.v=0;

    pcw_set_memory_pages();

    pcw_boot_timer=10;

    //Recargar contenido del boot de RAM
    rom_load(NULL);

    pcw_video_mode=0;

    pcw_init_colour_palette();
}

void pcw_init_memory_tables(void)
{
	DBG_PRINT_PCW VERBOSE_DEBUG,"Initializing pcw memory tables");

    z80_byte *puntero;
    puntero=memoria_spectrum;


    int i;
    for (i=0;i<PCW_MAX_RAM_PAGES;i++) {
            pcw_ram_mem_table[i]=puntero;
            puntero +=16384;
    }

}

void pcw_out_port_bank(z80_byte puerto_l,z80_byte value)
{
/*

&F0	O	Select bank for &0000
&F1	O	Select bank for &4000
&F2	O	Select bank for &8000
&F3	O	Select bank for &C000. Usually &87.
    */

    int bank=puerto_l-0xF0;



    pcw_bank_registers[bank]=value;

    //printf("PCW set bank %d value %02XH\n",bank,value);

    //if (bank==0) sleep(1);

    pcw_set_memory_pages();


}

void pcw_out_port_f4(z80_byte value)
{
    pcw_port_f4_value=value;
    //printf("PCW set port F4 value %02XH\n",value);

    pcw_set_memory_pages();
}

void pcw_out_port_f5(z80_byte value)
{
    pcw_port_f5_value=value;
    //printf("PCW set port F5 value %02XH\n",value);
}

void pcw_out_port_f6(z80_byte value)
{
    pcw_port_f6_value=value;
    //printf("PCW set port F6 value %02XH\n",value);
}


void pcw_out_port_f7(z80_byte value)
{
    /*if ((pcw_port_f7_value & 128) != (value&128)) {
        printf("Cambio inversion\n");
        sleep(1);
    }*/

    pcw_port_f7_value=value;
    //printf("PCW set port F7 value %02XH\n",value);

    modificado_border.v=1;


}


void pcw_interrupt_from_pd765(void)
{
    if (pcw_interrupt_from_pd765_type==1) {
        //printf("Generate NMI triggered from pd765\n");
        //
        generate_nmi();
        //sleep(2);

        //TODO: se supone que se desactivan todas al recibir una nmi
        /*
        Command 2 will enable non-maskable interrupts from the Floppy
        Disc Controller until Command 3 is issued to enable maskable
        interrupts instead, or Command 4 is issued to disable all
        interrupts from the Floppy Disc Controller, or until the first
        non-maskable interrupt occurs.  On power-up and system reset all
        Floppy Disc Controller Interrupts are disabled.
            */

        //TODO: No estoy seguro de lo siguiente
        //pcw_interrupt_from_pd765_type=0;
        //sleep(2);

    }

    //TODO Revisar esto. solo se genera cuando esta EI???
    if (pcw_interrupt_from_pd765_type==2) {
        //printf("Generate Maskable interrupt triggered from pd765\n");
        //If the Z80 has disabled interrupts, the interrupt line stays high until the Z80 enables them again.
        pcw_pending_interrupt.v=1;

        //temp
        //pcw_interrupt_from_pd765_type=0;
        //sleep(3);

    }
}

void pcw_out_port_f8(z80_byte value)
{

    //printf("PCW set port F8 value %02XH reg_pc %04XH\n",value,reg_pc);

    /*
    0 end bootstrap,
    1 reboot,
    2/3/4 connect FDC to NMI/standard interrupts/neither,
    5/6 set/clear FDC terminal count,
    7/8 screen on/off (for external video),
    9/10 disc motor on/off,
    11/12 beep on/off
    */

    switch (value) {
        case 0:
            DBG_PRINT_PCW VERBOSE_DEBUG,"Out port F8. End bootstrap");
            //sleep(2);
        break;

        case 1:
            DBG_PRINT_PCW VERBOSE_DEBUG,"Out port F8. Reboot");
            //sleep(2);

            reg_pc=0;

            //Recargar contenido del boot de RAM
            rom_load(NULL);
        break;

        /*
        Command 2 will enable non-maskable interrupts from the Floppy
        Disc Controller until Command 3 is issued to enable maskable
        interrupts instead, or Command 4 is issued to disable all
        interrupts from the Floppy Disc Controller, or until the first
        non-maskable interrupt occurs.  On power-up and system reset all
        Floppy Disc Controller Interrupts are disabled.
        */
        case 2:
            DBG_PRINT_PCW VERBOSE_PARANOID,"Out port F8. Connect FDC to NMI");
            pcw_interrupt_from_pd765_type=1;
            //sleep(2);
        break;

        case 3:
            DBG_PRINT_PCW VERBOSE_PARANOID,"Out port F8. Connect FDC to standard interrupts");
            pcw_interrupt_from_pd765_type=2;
            //sleep(2);
        break;

        case 4:
            DBG_PRINT_PCW VERBOSE_DEBUG,"Out port F8. Connect FDC to nothing");
            pcw_interrupt_from_pd765_type=0;
            //sleep(2);
        break;


        case 5:
            //printf("Set Terminal count\n");
            pd765_set_terminal_count_signal();
        break;

        case 6:
            //printf("Reset Terminal count\n");
            //Tal y como tenemos implementado el set, esto ya no tiene sentido
            //pd765_reset_terminal_count_signal();
        break;


        case 9:
            dsk_show_activity();
            pd765_motor_on();
        break;

        case 10:
            pd765_motor_off();
            DBG_PRINT_PCW VERBOSE_DEBUG,"Out port F8. Motor off on %04XH",reg_pc);
            //sleep(2);
        break;

        case 11:

            //Trivial pursuit por ejemplo reproduce musica con este beeper
            //printf("set beeper\n");
            value_beeper=100;
        break;

        case 12:

            //printf("reset beeper\n");
            value_beeper=0;
        break;

    }

}

void pcw_increment_interrupt_counter(void)
{
    //printf("scanline: %d F8 port: %02XH\n",t_scanline,pcw_port_f8_value);
    z80_byte pcw_interrupt_counter=pcw_port_f8_value & 0xF;

    if (pcw_interrupt_counter!=0x0F) pcw_interrupt_counter++;

    pcw_port_f8_value &=0xF0;
    pcw_port_f8_value |=pcw_interrupt_counter;

}

z80_byte pcw_get_port_f8_value(void)
{
    z80_byte return_value=pcw_port_f8_value;

    if (pd765_interrupt_pending) {

        //printf("Return F8 FDC interrupt\n");

        //printf("pd765_terminal_count_signal.v: %d\n",pd765_terminal_count_signal.v);
        return_value|=0x20;

        //sleep(1);
    }
    else {
        //printf("Return F8 FDC NO interrupt\n");
    }

    //bit 6 Frame flyback; this is set while the screen is not being drawn
    //TODO: de momento calculo chapucero
    //printf("t_scanline %d\n",t_scanline);
    if (t_scanline_draw<56) return_value |=0x40;

    return return_value;

}

z80_byte pcw_in_port_f8(void)
{
    //printf("LEE puerto F8H\n");
    return pcw_get_port_f8_value();


}


z80_byte pcw_in_port_f4(void)
{

    //printf("LEE puerto F4\n");

    z80_byte return_value=pcw_get_port_f8_value();

    pcw_port_f8_value &=0xF0;



    return return_value;

}

z80_byte pcw_in_port_fd(void)
{
    //printf("LEE puerto FDH on PC=%04XH\n",reg_pc);
    //Impresora. TODO completar
    return 0x40; //bit 6: If 1, printer has finished.
}

int pcw_keyboard_ticker_update_counter;

void pcw_keyboard_ticker_update(void)
{
	pcw_keyboard_ticker_update_counter++;


    //3FFFh bit 6 toggles with each update from the keyboard to the PCW.
    //3FFFh bit 7 is 1 if the keyboard is currently transmitting its state to the PCW, 0 if it is scanning its keys.
	pcw_keyboard_table[15] &= 0x3F;
    pcw_keyboard_table[15] |=0x40;
    pcw_keyboard_table[15] |=0x80;
	if (pcw_keyboard_ticker_update_counter & 1) pcw_keyboard_table[15] &= (255-0x80);
	if (pcw_keyboard_ticker_update_counter & 2) pcw_keyboard_table[15] &= (255-0x40);
}

z80_byte pcw_read_keyboard(z80_int dir)
{
    /*
The PCW's keyboard is directly mapped into the last 16 bytes of bank 3, even when interrupts are disabled.
Each key is reflected by one bit in bytes &3FF0-&3FFA.
b7:   k2     k1     [+]    .      ,      space  V      X      Z      del<   alt
b6:   k3     k5     1/2    /      M      N      B      C      lock          k.
b5:   k6     k4     shift  ;      K      J      F      D      A             enter
b4:   k9     k8     k7     ¤      L      H      G      S      tab           f8
b3:   paste  copy   #      P      I      Y      T      W      Q             [-]
b2:   f2     cut    return [      O      U      R      E      stop          can
b1:   k0     ptr    ]      -      9      7      5      3      2             extra
b0:   f4     exit   del>   =      0      8      6      4      1             f6
      &3FF0  &3FF1  &3FF2  &3FF3  &3FF4  &3FF5  &3FF6  &3FF7  &3FF8  &3FF9  &3FFA
Bytes &3FFB-&3FFF reflect the keyboard in a different, incomplete way. These bytes are also used by Creative Technology's KeyMouse (in its standard "MicroDesign mode") and the Teqniche 102-key keyboard to provide additional functionality, creating some incompatibilities along the way. Among the more interesting mappings are the following:
&3FFB	Standard keyboard
KeyMouse	b7-b0 unused (0)
b6-b0 horizontal movement counter.
&3FFC	KeyMouse	b7-b6 high bits of vertical movement counter.
&3FFD	All
Standard keyboard
KeyMouse	b7 always set; b6 current state of SHIFT LOCK
b3-b0 cursor keys, b4 matrix key
b3-b0 low bits of vertical movement counter.
&3FFE	KeyMouse	b7 left button, b6 right button.
    */

    //Quedarnos con ultimo byte de la direccion
    int fila=dir & 0xF;

    //de momento
    z80_byte return_value=255;

    //Teclas al pulsar activan bit


    /*

3FFFh bit 7 is 1 if the keyboard is currently transmitting its state to the PCW, 0 if it is scanning its keys.
If no keyboard is present, all 16 bytes of the memory map are zero.
    */

    //temp
    //if (fila==8) return_value=temp_row_8;
    //if (fila==5) return_value=temp_row_5;


    //si estamos en el menu, no devolver tecla
    if (!zxvision_key_not_sent_emulated_mach() ) {
        //if (fila==15) printf("leyendo fila 15\n");
        return_value=pcw_keyboard_table[fila];

    }

    //if (fila==0xF) sleep(1);

    //3FFDh bit 7 is 0 if LK2 is present, 1 if not.

    if (fila==0xD) return_value &=(255-128);

    //Si hay algun tipo de joystick de pcw habilitado, las teclas de cursor no retornarlas
    //Estos joysticks se pueden probar con juego Head Over Heels
    //DKtronics tambien en knight lore
    if (joystick_emulation==JOYSTICK_KEMPSTON || joystick_emulation==JOYSTICK_PCW_CASCADE
        || joystick_emulation==JOYSTICK_PCW_DKTRONICS || joystick_emulation==JOYSTICK_OPQA_SPACE) {
        if (fila==1) {
            //left y up
            return_value |= (128+64);
        }
        if (fila==0) {
            //right
            return_value |= 64;
        }
        if (fila==0xA) {
            //down
            return_value |= 64;
        }


    }

    //Soporte joystick opqa space. Muchos (la mayoria?) de opera soft
    if (joystick_emulation==JOYSTICK_OPQA_SPACE && !zxvision_key_not_sent_emulated_mach()) {
        if (fila==3) {
            if ((puerto_especial_joystick&1)) return_value &=(255-8);   //right. P
        }


        if (fila==4) {
            if ((puerto_especial_joystick&2)) return_value &=(255-4);   //left. O
        }

        if (fila==5) {
            if ((puerto_especial_joystick&16)) return_value &=(255-128);  //fire1. space
        }

        if (fila==8) {
            if ((puerto_especial_joystick&8)) return_value &=(255-8);   //up. Q
            if ((puerto_especial_joystick&4)) return_value &=(255-32);  //down. A
        }


    }



    //printf("PCW return read row %XH value %02XH reg_pc=%04XH\n",fila,return_value,reg_pc);
    //sleep(1);



    return return_value ^ 255;
}

//Puerto joystick kempston
z80_byte pcw_in_port_9f(void)
{
    if (joystick_emulation!=JOYSTICK_KEMPSTON) return 255;

    z80_byte valor_joystick=0;

    if (!zxvision_key_not_sent_emulated_mach() ) {

        if ((puerto_especial_joystick&1)) valor_joystick |=1;   //right
        if ((puerto_especial_joystick&2)) valor_joystick |=2;   //left
        if ((puerto_especial_joystick&4)) valor_joystick |=4;  //down
        if ((puerto_especial_joystick&8)) valor_joystick |=8;  //up
        if ((puerto_especial_joystick&16)) valor_joystick |=16;  //fire1

    }

    return valor_joystick;
}

//Puerto joystick cascade
z80_byte pcw_in_port_e0(void)
{
    if (joystick_emulation!=JOYSTICK_PCW_CASCADE) return 255;

    z80_byte valor_joystick=255;

    if (!zxvision_key_not_sent_emulated_mach() ) {

        if ((puerto_especial_joystick&1)) valor_joystick &=(255-2);   //right
        if ((puerto_especial_joystick&2)) valor_joystick &=(255-1);   //left
        if ((puerto_especial_joystick&4)) valor_joystick &=(255-4);  //down
        if ((puerto_especial_joystick&8)) valor_joystick &=(255-16);  //up
        if ((puerto_especial_joystick&16)) valor_joystick &=(255-128);  //fire1

    }

    return valor_joystick;
}

z80_byte pcw_in_port_dktronics_joystick(void)
{
    //Retornar tal cual registro del chip
    if (joystick_emulation!=JOYSTICK_PCW_DKTRONICS) return in_port_ay(0xFF);

    z80_byte valor_joystick=255;

    if (!zxvision_key_not_sent_emulated_mach() ) {

        if ((puerto_especial_joystick&1)) valor_joystick &=(255-8);   //right
        if ((puerto_especial_joystick&2)) valor_joystick &=(255-4);   //left
        if ((puerto_especial_joystick&4)) valor_joystick &=(255-16);  //down
        if ((puerto_especial_joystick&8)) valor_joystick &=(255-32);  //up
        if ((puerto_especial_joystick&16)) valor_joystick &=(255-64);  //fire1

    }

    return valor_joystick;
}

void pcw_putpixel_border(int x,int y,unsigned int color)
{

    scr_putpixel(x,y,color);

}

void scr_refresca_border_pcw(unsigned int color)
{


    int ancho_pantalla=PCW_DISPLAY_WIDTH;
    int alto_pantalla=PCW_DISPLAY_HEIGHT;

	int ancho_border=PCW_LEFT_BORDER_NO_ZOOM;
	int alto_border=PCW_TOP_BORDER_NO_ZOOM;

	//printf ("ancho pantalla: %d alto_pantalla: %d offset_x_pantalla: %d anchoborder: %d altoborder: %d\n",ancho_pantalla,alto_pantalla,offset_x_pantalla,ancho_border,alto_border);



    int x,y;

	int x_borde_derecho=(ancho_border+ancho_pantalla)*zoom_x;
	//printf ("x borde derecho: %d total ventana: %d\n",x_borde_derecho,(640+CPC_LEFT_BORDER_NO_ZOOM*2)*zoom_x);


    //parte superior e inferior
    for (y=0;y<alto_border*zoom_y;y++) {
        for (x=0;x<(PCW_DISPLAY_WIDTH+PCW_LEFT_BORDER_NO_ZOOM*2)*zoom_x;x++) {
        //printf ("x: %d y: %d\n",x,y);
        pcw_putpixel_border(x,y,color);
        pcw_putpixel_border(x,(alto_border+alto_pantalla)*zoom_y+y,color);
        }
    }

    //laterales

    for (y=0;y<alto_pantalla*zoom_y;y++) {
        for (x=0;x<ancho_border*zoom_x;x++) {
            pcw_putpixel_border(x,alto_border*zoom_y+y,color);
            pcw_putpixel_border(x_borde_derecho+x,alto_border*zoom_y+y,color);
        }

    }


}




void scr_refresca_pant_pcw_return_line_pointer(z80_byte roller_ram_bank,z80_int roller_ram_offset,
    z80_byte *address_block, z80_int *address)
{
    z80_byte *puntero_roller_ram;

    puntero_roller_ram=pcw_ram_mem_table[roller_ram_bank];
    puntero_roller_ram +=roller_ram_offset;

    z80_int valor=*puntero_roller_ram+256*puntero_roller_ram[1];

    //descomponer en bloque y addres

    *address_block=(valor>>13) & 0x07;

    //15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0
    //block   |  offset /16                 | offset

    *address=(valor & 7) +2 *(valor & 0x1FF8);
}



/*
Modo 0 (720x256x2), el nativo del PCW: negro y verde, o Negro y blanco, a elegir vía puente.
Modo 1 (360x256x4): los colores de la paleta 1 con intensidad del modo de 4 colores de la CGA.
Modo 2 (180x256x16): los colores de la paleta de la CGA (los 16 clásicos que trae por defecto la EGA).
Modo 3 (360x256x16): los mismos colores que el modo 2. Aquí tenemos píxeles cuadrados, como el modo 1
    pero con los colores del modo 2. ¿La trampa? Es un modo de atributos: no podemos tener más de dos colores
    distintos en una celda 1x8. Facilita mucho portar cosas de Spectrum, MSX, Thomson, … Y si mantenemos los colores
    tenemos la mitad de memoria de vídeo, con lo que es más rápido el volcado, movimiento de sprites, etc.
*/


int pcw_video_mode=0;

char *pcw_video_mode_names[]={
    "0 720x256x2",
    "1 360x256x4",
    "2 180x256x16",
    "3 360x256x16"
};

//Refresco sin rainbow
void scr_refresca_pantalla_pcw(void)
{


    //Address of roller RAM. b7-5: bank (0-7). b4-1: address / 512.
    z80_byte roller_ram_bank=(pcw_port_f5_value >> 5) & 0x07;

    z80_int roller_ram_offset=(pcw_port_f5_value & 0x1F) * 512;

    //printf("Roller ram: bank: %02XH Offset: %02XH\n",roller_ram_bank,roller_ram_offset);


    int x,y,scanline;

    for (y=0;y<256;y+=8) {
        z80_byte address_block;
        z80_int address;


        //roller_ram_offset+=2;
        for (x=0;x<720;x+=8) {

            for (scanline=0;scanline<8;scanline++) {
                int yfinal=y+scanline;

                //Tener en cuenta scroll vertical
                // puerto F6
                //Ejemplo de juego que usa scroll vertical: skywar.dsk

                z80_byte linea_scroll=pcw_port_f6_value;

                if (pcw_do_not_scroll.v) linea_scroll=0;

                int linea_roller_bank_elegir=(yfinal+linea_scroll) % 256;

                int offset_a_linea_roller_bank=linea_roller_bank_elegir*2; //*2 porque hay punteros de 16 bits


                scr_refresca_pant_pcw_return_line_pointer(roller_ram_bank,roller_ram_offset+offset_a_linea_roller_bank,&address_block,&address);

                address +=x;


                z80_byte *puntero_byte=pcw_ram_mem_table[address_block]+address;

                z80_byte byte_leido=*puntero_byte;

                //Reverse video
                //Ejemplo de juego que usa reverse video: skywar.dsk
                if ((pcw_port_f7_value & 0x80) && pcw_do_not_inverse_display.v==0) {
                    byte_leido ^=255;
                }

                //Si pantalla no activa
                if (!(pcw_port_f7_value & 0x40) && pcw_always_on_display.v==0) byte_leido=0;

                int bit;
                int pixel_color;


                //Modo 0 (720x256x2), el nativo del PCW: negro y verde, o Negro y blanco, a elegir vía puente.
                if (pcw_video_mode==0) {
                    for (bit=0;bit<8;bit++) {

                        if (byte_leido & 128) pixel_color=1;
                        else pixel_color=0;

                        pcw_refresca_putpixel_mode0(x+bit,y+scanline,pixel_color);

                        byte_leido=byte_leido<<1;
                    }
                }

                //Modo 1 (360x256x4): los colores de la paleta 1 con intensidad del modo de 4 colores de la CGA
                if (pcw_video_mode==1) {
                    for (bit=0;bit<8;bit+=2) {

                        pixel_color=(byte_leido>>6) & 3;

                        //Resolucion efectiva a mitad. en pantalla seguira siendo 720, solo que dos pixeles repetidos
                        pcw_refresca_putpixel_mode1(x+bit,y+scanline,pixel_color);
                        pcw_refresca_putpixel_mode1(x+bit+1,y+scanline,pixel_color);

                        byte_leido=byte_leido<<2;
                    }
                }

                //Modo 2 (180x256x16): los colores de la paleta de la CGA (los 16 clásicos que trae por defecto la EGA).
                if (pcw_video_mode==2) {
                    for (bit=0;bit<8;bit+=4) {

                        pixel_color=(byte_leido>>4) & 0xF;

                        //Resolucion efectiva a 1/4. en pantalla seguira siendo 720, solo que cuatro pixeles repetidos
                        pcw_refresca_putpixel_mode2(x+bit,y+scanline,pixel_color);
                        pcw_refresca_putpixel_mode2(x+bit+1,y+scanline,pixel_color);
                        pcw_refresca_putpixel_mode2(x+bit+2,y+scanline,pixel_color);
                        pcw_refresca_putpixel_mode2(x+bit+3,y+scanline,pixel_color);

                        byte_leido=byte_leido<<4;
                    }
                }

                /*
                Modo 3 (360x256x16): los mismos colores que el modo 2. Aquí tenemos píxeles cuadrados, como el modo 1
                pero con los colores del modo 2. ¿La trampa? Es un modo de atributos: no podemos tener más de dos colores
                distintos en una celda 1x8. Facilita mucho portar cosas de Spectrum, MSX, Thomson, … Y si mantenemos los colores
                tenemos la mitad de memoria de vídeo, con lo que es más rápido el volcado, movimiento de sprites, etc.
                En este modo tenemos 360x256x16; sí, con 16 colores en vez de 4, a costa de sólo poder elegir dos en cada bloque 8x1.
                Funciona interpretando el primer byte como los atributos de fondo y color (4 bits cada uno),
                y el segundo como los 8 pixels a dibujar, fondo o color anteriormente especificado. De esta forma no se renuncia
                a la resolución para conseguir más colores.
                */
                if (pcw_video_mode==3) {

                    //Dado que nuestro bucle con x salta de 8 en 8 hasta 720, y este modo es de 360,
                    //tenemos que pillar cada x=0,x=16,etc
                    if ((x%16)==0) {
                        //Primer byte contiene los atributos
                        int papel=(byte_leido>>4)&15;
                        int tinta=byte_leido & 15;


                        //Obtener el siguiente byte, que contiene los pixeles
                        address +=8;

                        puntero_byte=pcw_ram_mem_table[address_block]+address;

                        byte_leido=*puntero_byte;


                        for (bit=0;bit<8;bit++) {

                            if (byte_leido & 128) pixel_color=tinta;
                            else pixel_color=papel;

                            //Doble de ancho
                            pcw_refresca_putpixel_mode2(x+bit*2,y+scanline,pixel_color);
                            pcw_refresca_putpixel_mode2(x+bit*2+1,y+scanline,pixel_color);

                            byte_leido=byte_leido<<1;
                        }
                    }



                }
            }
        }
    }
}

void scr_refresca_pantalla_y_border_pcw_no_rainbow(void)
{

    //Refrescar border si conviene
    if (border_enabled.v) {
        if (modificado_border.v) {


            int border_col=0;

            //Reverse video
            //Ejemplo de juego que usa reverse video: skywar.dsk
            if ((pcw_port_f7_value & 0x80) && pcw_do_not_inverse_display.v==0) border_col=1;

            int color;

            //Ejemplo de juego que usa color modo 1: knight lore
            if (pcw_video_mode==0) color=pcw_get_rgb_color_mode0(border_col);
            else if (pcw_video_mode==1) color=pcw_get_rgb_color_mode1(border_col*3);
            else color=pcw_get_rgb_color_mode2(border_col*15);


            scr_refresca_border_pcw(color);
            modificado_border.v=0;
        }
	}


    scr_refresca_pantalla_pcw();
}



void scr_refresca_pantalla_y_border_pcw(void)
{

    //TODO : de momento sin rainbow
    scr_refresca_pantalla_y_border_pcw_no_rainbow();

    /*
    if (rainbow_enabled.v) {
        scr_refresca_pantalla_y_border_pcw_rainbow();
    }
    else {
        scr_refresca_pantalla_y_border_pcw_no_rainbow();
    }
    */
}

int pcw_was_booting_disk_enabled=0;
z80_int pcw_was_booting_disk_address=0;
//Nombre anterior que habia antes de insertar disco boot
char dskplusthree_before_boot_file_name[PATH_MAX]="";

//Si antes de insertar habia disco
z80_bit dskplusthree_emulation_before_boot={0};

//Si hay que reinsertar disco previo despues de boot
z80_bit pcw_boot_reinsert_previous_dsk={1};

//Si hay que cargar cpm cuando disco no arrancable
z80_bit pcw_failback_cpm_when_no_boot={1};


//Comprueba cuando se ha iniciado del todo el disco de CP/M y reinserta el disco anterior que habia
void pcw_handle_end_boot_disk(void)
{
    if (!pcw_was_booting_disk_enabled) return;

    if (reg_pc==pcw_was_booting_disk_address) {
        DBG_PRINT_PCW VERBOSE_DEBUG,"Reached end of boot");
        pcw_was_booting_disk_enabled=0;


        //Si habia disco insertado antes, reinsertar
        if (dskplusthree_emulation_before_boot.v) {
            DBG_PRINT_PCW VERBOSE_DEBUG,"Reinserting disk before boot: %s",dskplusthree_before_boot_file_name);

            //Desactivamos autoload para que no haga reset
            int antes_noautoload=noautoload.v;
            noautoload.v=1;
            dskplusthree_disable();

    		dsk_insert_disk(dskplusthree_before_boot_file_name);

	    	dskplusthree_enable();

            noautoload.v=antes_noautoload;
        }

    }
}

void pcw_boot_dsk_generic(char *filename,z80_int address_end_boot)
{


	char buffer_nombre[PATH_MAX];

	if (find_sharedfile(filename,buffer_nombre)) {
        //Copiar nombre anterior de disco, y restaurarlo despues de haber hecho boot totalmente
        strcpy(dskplusthree_before_boot_file_name,dskplusthree_file_name);
        dskplusthree_emulation_before_boot.v=dskplusthree_emulation.v;

        dskplusthree_disable();

		dsk_insert_disk(buffer_nombre);

		dskplusthree_enable();
		pd765_enable();

        //esté o no el autoload, hacemos reset
        reset_cpu();


        if (address_end_boot && pcw_boot_reinsert_previous_dsk.v) {
            pcw_was_booting_disk_enabled=1;
            pcw_was_booting_disk_address=address_end_boot;
        }

	}

	else {
        DBG_PRINT_PCW VERBOSE_ERR,"%s image not found",filename);
	}
}

void pcw_boot_locoscript(void)
{
    //Con locoscript no hacemos cambio de disco al finalizar
    pcw_boot_dsk_generic("pcw_8x_boot1.dsk",0);
}

void pcw_boot_cpm(void)
{
    pcw_boot_dsk_generic("pcw_8x_boot2.dsk",0x607);
}


//Cuenta segundos desde el boot
int pcw_boot_timer=0;

//Llamado desde el timer cada segundo
void pcw_boot_timer_handle(void)
{

    if (pcw_boot_timer==0) return;

    pcw_boot_timer--;

    DBG_PRINT_PCW VERBOSE_DEBUG,"pcw_boot_timer %d",pcw_boot_timer);

}

//Comprueba que el disco no sea arrancable (cuando llega a ciertas direcciones y ejecuta opcode concreto)
//Y en ese caso puede hacer fallback a CP/M
//Tambien detectar si no hay disco insertado
void pcw_boot_check_dsk_not_bootable(void)
{

    //Si ha pasado ya el tiempo desde el boot, no comprobar mas

    if (pcw_boot_timer==0) return;


    if (reg_pc!=0x3D && reg_pc!=0xF13B && reg_pc!=0xF9) return;

    int autoboot=0;

    //Cuando no hay disco insertado
    //OUT (C),A, cuando hace el recalibrate
    /*if (reg_pc==0xF9 && peek_byte_no_time(reg_pc)==0xED && peek_byte_no_time(reg_pc+1)==0x79) {
        pcw_boot_timer=0;
        printf("Seems you do not have selected any DSK\n");
        autoboot=1;
    }*/

    /*
    3DH BIT 7,(HL)
    CB 7E
    */

   if (peek_byte_no_time(reg_pc)==0xCB && peek_byte_no_time(reg_pc+1)==0x7E) {
        pcw_boot_timer=0;
        DBG_PRINT_PCW VERBOSE_INFO,"Seems you have selected a non bootable disk");

        autoboot=1;
   }

    //Si hay que autoinsertar cpm
    if (autoboot & pcw_failback_cpm_when_no_boot.v) {
        DBG_PRINT_PCW VERBOSE_INFO,"Autobooting CP/M");
        //sleep(2);
        pcw_boot_cpm();

        //Y no autodetectar de nuevo si disco no botable,
        //Esto no deberia suceder, pues CP/M es botable,
        //pero si por algo fallase, nos quedariamos en un bucle continuo de reinicios
        pcw_boot_timer=0;
    }

}







void pcw_out_port_video(z80_byte puerto_l,z80_byte value)
{

    if (puerto_l==0x80) {
        pcw_last_port_80_value=value;

        if (value>=16) {
            pcw_last_index_color_change=value & 0x0F;
            pcw_last_index_color_change_component=0;

            //printf("Cambio indice a %d component %d\n",pcw_last_index_color_change,pcw_last_index_color_change_component);
        }
    }

    if (puerto_l==0x81) {
        pcw_last_port_81_value=value;


        if (pcw_last_port_80_value & 0x20) {
            //Cambio color paleta mediante indice a colores

            int indice_a_color;

            indice_a_color=pcw_last_index_color_change;

            //por si acaso
            if (indice_a_color>15) indice_a_color=0;

            int valor_a_cambiar=spectrum_colortable_normal[PCW_RGB8_FIRST_COLOR+value];

            //printf("Cambio color paleta con indice %d por RGB=%06X\n",indice_a_color,valor_a_cambiar);

            pcw_change_palette_colour(indice_a_color,valor_a_cambiar);


            pcw_last_index_color_change++;
            if (pcw_last_index_color_change>=16) pcw_last_index_color_change=0;
        }

        else if (pcw_last_port_80_value & 0x10) {
            //Cambio color paleta mediante RGB


            int indice_a_color;
            int componente;


            indice_a_color=pcw_last_index_color_change;
            componente=pcw_last_index_color_change_component;

            //por si acaso
            if (indice_a_color>15) indice_a_color=0;
            if (componente>2) componente=0;

            //Llegan en orden B,G,R
            //Si componente=0, red. si 1, green, si 2, blue
            int valor_a_cambiar=pcw_get_palette_colour(indice_a_color);
            int rotaciones=componente;
            rotaciones *=8;
            //Si componente=0 (RED), rotaciones=2. si componente=1, rotaciones=8

            int mascara_quitar=0xFF << rotaciones;
            mascara_quitar ^=0xFFFFFF;

            int valor_aplicar=value << rotaciones;

            valor_a_cambiar &=mascara_quitar;
            valor_a_cambiar |=valor_aplicar;

            //printf("Cambio color paleta con rgb %d (component %d) component value=%02X por RGB=%06X\n",indice_a_color,pcw_last_index_color_change_component,value,valor_a_cambiar);

            pcw_change_palette_colour(indice_a_color,valor_a_cambiar);

            pcw_last_index_color_change_component++;

            if (pcw_last_index_color_change_component>=3) {
                pcw_last_index_color_change_component=0;

                pcw_last_index_color_change++;
                if (pcw_last_index_color_change>=16*3) pcw_last_index_color_change=0;
            }
        }

        else {
            //Cambio modo
            if (value>=4) value=0;

            pcw_video_mode=value;

            char buffer_mensaje[256];
            sprintf(buffer_mensaje,"Setting video mode %s",pcw_video_mode_names[pcw_video_mode]);
            screen_print_splash_text_center_no_if_previous(ESTILO_GUI_TINTA_NORMAL,ESTILO_GUI_PAPEL_NORMAL,buffer_mensaje);

        }
    }

}

z80_byte pcw_in_port_video(z80_byte puerto_l)
{
    if (puerto_l==0x80) return pcw_last_port_80_value;
    if (puerto_l==0x81) return pcw_last_port_81_value;

    //Aqui no deberia llegar
    return 255;
}
