/* Nintendo Wi-Fi Connection for the port (wifi_port.c): what the front end sets before the
 * run, and what it keeps between runs. */
#ifndef ACWW_PORT_WIFI_PORT_H
#define ACWW_PORT_WIFI_PORT_H

#include <stddef.h>
#include <stdint.h>

/* The console's connection settings in the firmware image: four pages below the user
 * settings (three connections and a spare), which the game's library reads and writes. */
#define WIFI_PORT_SETTINGS_AT 0x3fa00u
#define WIFI_PORT_SETTINGS_SIZE 0x400u

/* Before the run: Wi-Fi Connection on. `dns`: the name server the console's queries go to,
 * "a.b.c.d" or "a.b.c.d:port" (NULL or empty: this machine's own resolver). `mac`: the
 * console's address, or NULL for the model's own. `saved`: the connection settings of the
 * runs before (WIFI_PORT_SETTINGS_SIZE bytes), or NULL. */
void wifi_port_enable(const char *dns, const uint8_t mac[6], const uint8_t *saved, size_t saved_size);

/* The connection settings as they are now, to be kept (WIFI_PORT_SETTINGS_SIZE bytes), or
 * NULL when Wi-Fi Connection is off or the run has not started. */
const uint8_t *wifi_port_settings(void);

/* wm_port.c asks: the console's address, if the front end gave one. */
int wifi_port_mac(uint8_t mac[6]);

void wifi_port_status(char *text, size_t size);

#endif
