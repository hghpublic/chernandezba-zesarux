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

#ifndef ZENG_ONLINE_CLIENT_H
#define ZENG_ONLINE_CLIENT_H

#include "zeng_online.h"
#include "stats.h"


extern int zeng_online_client_list_rooms_thread_running;
extern int zeng_online_client_create_room_thread_running;
extern int zeng_online_client_join_room_thread_running;
extern int zeng_online_client_join_list_thread_running;
extern int zeng_online_client_authorize_join_thread_running;
extern int zeng_online_client_leave_room_thread_running;
extern int zeng_online_client_destroy_room_thread_running;
extern int zeng_online_client_autojoin_room_thread_running;
extern int zeng_online_client_disable_autojoin_room_thread_running;
extern int zeng_online_client_write_message_room_thread_running;
extern int zeng_online_client_allow_message_room_thread_running;
extern int zeng_online_client_list_users_thread_running;
extern int zeng_online_client_get_profile_keys_thread_running;
extern int zeng_online_client_send_profile_keys_thread_running;
extern int zeng_online_client_kick_user_thread_running;
extern int zeng_online_client_max_players_room_thread_running;
extern int zeng_online_client_rename_room_thread_running;
extern int zeng_online_client_stop_slave_thread_thread_running;
extern int zeng_online_client_stop_master_thread_thread_running;

extern void zeng_online_client_list_rooms(void);
extern void zeng_online_client_create_room(int room_number,char *room_name);
extern void zeng_online_client_join_room(int room_number,char *creator_password);
extern void zeng_online_client_leave_room(void);
extern void zeng_online_client_authorize_join(int permissions);
extern void zeng_online_client_join_list(void);
extern void zeng_online_client_destroy_room(void);
extern void zeng_online_client_autojoin_room(int permisos);
extern void zeng_online_client_disable_autojoin_room(void);
extern void zeng_online_client_write_message_room(char *message);
extern void zeng_online_client_allow_message_room(int allow_disallow);
extern void zeng_online_client_list_users(void);
extern void zeng_online_client_get_profile_keys(void);
extern void zeng_online_client_send_profile_keys(void);
extern void zeng_online_client_kick_user(char *message);
extern void zeng_online_client_max_players_room(int valor);
extern void zeng_online_client_rename_room(char *name);
extern void zeng_online_client_stop_slave_thread(void);
extern void zeng_online_client_stop_master_thread(void);

extern void zeng_online_client_apply_pending_received_snapshot(void);

extern void zeng_online_client_alter_fps_streaming(void);

extern void zec_veces_audio_no_recibido_timer(void);

extern int zeng_online_client_get_snapshot_applied_finished;
extern void zoc_slave_set_force_get_snapshot(void);

extern int zoc_slave_forbid_apply_streaming_video;

extern int zeng_online_snapshot_diff;

extern int zeng_online_snapshot_diff_media;

extern int streaming_enabled_when_creating;
extern int streaming_silence_detection;

extern void zec_veces_audio_no_recibido_timer(void);

extern int zoc_streaming_display_received_counter;
extern int zoc_streaming_audio_received_counter;
extern int zoc_common_alive_user_send_counter;
extern int zoc_common_get_messages_received_counter;
extern int zoc_send_streaming_display_counter;
extern int zoc_send_streaming_audio_counter;
extern int zoc_sent_snapshots_counter;
extern int zoc_keys_send_counter;
extern int zoc_get_keys_counter;
extern int zoc_received_snapshot_counter;
extern int zoc_get_pending_authorization_counter;
extern int zoc_streaming_display_differential_received_counter;
extern int zoc_streaming_display_full_received_counter;
extern int zoc_streaming_audio_no_silence_received_counter;
extern int zoc_streaming_audio_silence_received_counter;
extern int zoc_generated_differential_displays_counter;
extern int zoc_generated_full_displays_counter;
extern int zoc_sent_streaming_audio_silence_counter;
extern int zoc_sent_streaming_audio_no_silence_counter;

extern int created_room_streaming_mode;

//Para poder hacer debug_printf con la clase adecuada
#define DBG_PRINT_ZENG_ONLINE_CLIENT debug_printf(VERBOSE_CLASS_ZENG_ONLINE_CLIENT|

//#define ZENG_ONLINE_USE_ZIP_SNAPSHOT

extern z80_bit zeng_online_room_was_created;

extern int allowed_keys[ZOC_MAX_KEYS_PROFILES][ZOC_MAX_KEYS_ITEMS];
extern char allowed_keys_assigned[ZOC_MAX_KEYS_PROFILES][STATS_UUID_MAX_LENGTH+1];

extern char *zoc_return_connected_status(void);

extern char *zeng_remote_list_users_buffer;

extern void zoc_stop_master_thread(void);
extern void zoc_stop_slave_thread(void);

//Valor es evidente pero por no repetir siempre el mismo valor
#define ZOC_STREAM_DISPLAY_SIZE 6912

extern int zoc_slave_differential_displays_limit_full;

extern z80_bit zoc_slave_differential_displays_limit_full_autoadjust;

//extern void zoc_start_snapshot_sending(void);

extern char zeng_remote_join_list_buffer[];

extern z80_bit zeng_online_i_am_master;
//extern z80_bit zeng_online_i_am_joined;
extern int zeng_online_joined_to_room_number;
extern z80_bit zeng_online_connected;

extern char zeng_online_server[];

extern char zeng_online_nickname[];

extern int zoc_rejoining_as_master;

extern char *zeng_remote_list_rooms_buffer;

extern int zeng_online_scanline_counter;

extern void zoc_start_master_thread(void);
extern void zoc_start_slave_thread(void);
extern void zeng_online_client_end_frame_from_core_functions(void);
extern int zoc_last_snapshot_received_counter;
extern long zeng_online_get_last_list_rooms_latency(void);

extern char created_room_creator_password[];
extern int created_room_user_permissions;
extern void zoc_show_bottom_line_footer_connected(void);

extern void zeng_online_client_end_audio_frame_stuff(void);




//5 segundos de timeout, para aceptar teclas slave si no hay snapshot
#define ZOC_TIMEOUT_NO_SNAPSHOT 250

#endif
