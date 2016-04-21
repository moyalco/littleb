#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <systemd/sd-bus.h>

#define MAX_LEN 256
#define MAX_OBJECTS 256

typedef struct bl_device
{
        const char* object_path;
        const char* address;
        const char* name;
} bl_device;

sd_bus_error error = SD_BUS_ERROR_NULL;
sd_bus *bus = NULL;
bl_device** devices = NULL;
int devices_size = 0;

void
close_system_bus()
{
        sd_bus_unref(bus);
        if(devices) {
                free(devices);
        }
}

const char*
convert_object_path_to_address(const char* address)
{
        int i;
        const char* prefix = "dev_";

        // find the address in the object string - after "dev_"
        const char * start_of_address = strstr(address, prefix) + strlen(prefix) * sizeof(char);

        char* new_address = strdup(start_of_address);

        if(new_address == NULL) {
                fprintf(stderr, "Error copying address to new_addresss\n");
                return NULL;
        }

        for(i = 0; i < strlen(new_address); i++) {
                if(new_address[i] == '_') {
                        new_address[i] = ':';
                }
        }

        return (const char*) new_address;
}

int
open_system_bus()
{
        int r;
        /* Connect to the system bus */
        r = sd_bus_open_system(&bus);
        if(r < 0) {
                fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-r));
        }
        return r;
}

bool
is_bus_connected()
{
        if(bus == NULL) {
                return false;
        }
        else {
                return (sd_bus_is_open(bus)) ? true : false;
        }
}

const char*
get_device_name(const char* device_path)
{
        printf("Method Called: %s\n", __FUNCTION__);
        int r;
        char* name;

        r = sd_bus_get_property_string(bus, "org.bluez", device_path, "org.bluez.Device1", "Name", &error, &name);
        if(r < 0) {
                fprintf(stderr, "Failed to issue method call: %s on %s\n", error.message, device_path);
                return NULL;
        }

        return (const char *) name;
}

bool
is_bl_device(const char* object_path)
{
        printf("Method Called: %s\n", __FUNCTION__);

        sd_bus_message *m = NULL;
        int r;
        const char* introspect_xml;

        if(!is_bus_connected()) {
                fprintf(stderr, "Bus is not opened\n");
        }

        r = sd_bus_call_method(bus,
                               "org.bluez",
                               object_path,
                               "org.freedesktop.DBus.Introspectable",
                               "Introspect",
                               &error,
                               &m,
                               NULL);
        if(r < 0) {
                fprintf(stderr, "Failed to issue method call: %s\n", error.message);
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);;
                return false;
        }

        r = sd_bus_message_read_basic(m, 's', &introspect_xml);
        if(r < 0) {
                fprintf(stderr, "sd_bus_message_read_basic: %s\n", strerror(-r));
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return false;
        }

        sd_bus_error_free(&error);
        sd_bus_message_unref(m);

        return (strstr(introspect_xml, "org.bluez.Device1") != NULL) ? true : false;
}

int
add_new_device(const char* device_path)
{
        printf("Method Called: %s\n", __FUNCTION__);

        int current_index = devices_size;
        if(devices_size == 0 || devices == NULL) {
                devices = malloc(sizeof(bl_device*));
                devices_size++;
        }
        else {
                devices_size++;
                devices = realloc(devices, (devices_size) * sizeof(bl_device*));
                if(devices == NULL) {
                        fprintf(stderr, "Error reallocating memory for devices\n");
                        return EXIT_FAILURE;
                }
        }

        bl_device* new_device = malloc(sizeof(bl_device));

        new_device->object_path = device_path;
        const char* name = get_device_name(device_path);
        if(name == NULL) {
                fprintf(stderr, "Error couldn't find device name\n");
                new_device->name = "null";
        }
        else {
                new_device->name = name;
        }
        new_device->address = convert_object_path_to_address(device_path);
        devices[current_index] = new_device;

        return EXIT_SUCCESS;
}

int
get_root_objects(const char** objects)
{
        printf("Method Called: %s\n", __FUNCTION__);
        int r;
        const char *object_path;
        int i = 0;
        sd_bus_message *m = NULL;

        if(!is_bus_connected()) {
                fprintf(stderr, "Bus is not opened\n");
        }

        r = sd_bus_call_method(bus,
                               "org.bluez",
                               "/",
                               "org.freedesktop.DBus.ObjectManager",
                               "GetManagedObjects",
                               &error,
                               &m,
                               NULL);
        if(r < 0) {
                fprintf(stderr, "Failed to issue method call: %s\n", error.message);
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        /* Parse the response message */
        //"a{oa{sa{sv}}}"
        r = sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}");
        if(r < 0) {
                fprintf(stderr, "sd_bus_message_enter_container {oa{sa{sv}}}: %s\n", strerror(-r));
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        while((r = sd_bus_message_enter_container(m, 'e', "oa{sa{sv}}")) > 0) {
                r = sd_bus_message_read_basic(m, 'o', &object_path);
                if(r < 0) {
                        fprintf(stderr, "sd_bus_message_read_basic: %s\n", strerror(-r));
                                sd_bus_error_free(&error);
        sd_bus_message_unref(m);;
                }
                else {
                        char* restrict new_object_path = malloc(strlen(object_path) + 1);
                        if(new_object_path == NULL) {
                                fprintf(stderr, "Error allocating memory for object name\n");
                                sd_bus_error_free(&error);
                                sd_bus_message_unref(m);
                                return EXIT_FAILURE;
                        }
                        objects[i] = strcpy(new_object_path, object_path);
                        i++;
                }

                r = sd_bus_message_skip(m, "a{sa{sv}}");
                if(r < 0) {
                        fprintf(stderr, "sd_bus_message_skip: %s\n", strerror(-r));
                        sd_bus_error_free(&error);
                        sd_bus_message_unref(m);
                        return EXIT_FAILURE;
                }

                r = sd_bus_message_exit_container(m);
                if(r < 0) {
                        fprintf(stderr, "sd_bus_message_exit_container oa{sa{sv}}: %s\n", strerror(-r));
                        sd_bus_error_free(&error);
                        sd_bus_message_unref(m);
                        return EXIT_FAILURE;
                }
        }

        if(r < 0) {
                fprintf(stderr, "sd_bus_message_enter_container oa{sa{sv}}: %s\n", strerror(-r));
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        r = sd_bus_message_exit_container(m);
        if(r < 0) {
                fprintf(stderr, "sd_bus_message_exit_container {oa{sa{sv}}}: %s\n", strerror(-r));
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        sd_bus_error_free(&error);
        sd_bus_message_unref(m);
        return EXIT_SUCCESS;
}

int
scan_devices(int seconds)
{
        printf("Method Called: %s\n", __FUNCTION__);
        int r;
        sd_bus_message *m = NULL;

        if(!is_bus_connected()) {
                fprintf(stderr, "Bus is not opened\n");
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        r = sd_bus_call_method(bus, "org.bluez", "/org/bluez/hci0", "org.bluez.Adapter1", "StartDiscovery", &error, &m,
        NULL);
        if(r < 0) {
                fprintf(stderr, "Failed to issue method call: %s\n", error.message);
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        sleep(seconds);

        r = sd_bus_call_method(bus, "org.bluez", "/org/bluez/hci0", "org.bluez.Adapter1", "StopDiscovery", &error, &m,
        NULL);

        if(r < 0) {
                fprintf(stderr, "Failed to issue method call: %s\n", error.message);
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        sd_bus_error_free(&error);
        sd_bus_message_unref(m);
        return EXIT_SUCCESS;
}

int
list_devices(int seconds)
{
        printf("Method Called: %s\n", __FUNCTION__);
        const char** objects;
        const char* point;
        int i, r;

        if(devices != NULL) {
                free(devices);
        }

        objects = malloc(MAX_OBJECTS * sizeof(const char *));
        if(objects == NULL) {
                fprintf(stderr, "Error allocating memory for objects array\n");
                return EXIT_FAILURE;
        }

        scan_devices(seconds);

        get_root_objects(objects);

        //if (r < 0) {
        //	fprintf(stderr, "Error getting root objects\n");
        //	free(objects);
        //	return -1;
        //}

        while(objects[i] != NULL) {
                if(is_bl_device(objects[i])) {
                        add_new_device(objects[i]);
                }
                i++;
        }

        free(objects);
        return EXIT_SUCCESS;
}

int
connect_device(const char * address)
{
        printf("Method Called: %s\n", __FUNCTION__);
        int r;
        sd_bus_message *m = NULL;

        if(!is_bus_connected()) {
                fprintf(stderr, "Bus is not opened\n");
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        r = sd_bus_call_method(bus,
                               "org.bluez",
                               address,
                               "org.bluez.Device1",
                               "Connect",
                               &error,
                               &m,
                               NULL);

        if(r < 0) {
                fprintf(stderr, "Failed to issue method call: %s\n", error.message);
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        sd_bus_error_free(&error);
        sd_bus_message_unref(m);
        return EXIT_SUCCESS;
}

int
disconnect_device(const char * address)
{
        printf("Method Called: %s\n", __FUNCTION__);
        int r;
        sd_bus_message *m = NULL;

        if(!is_bus_connected()) {
                fprintf(stderr, "Bus is not opened\n");
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        r = sd_bus_call_method(bus,
                               "org.bluez",
                               address,
                               "org.bluez.Device1",
                               "Disconnect",
                               &error,
                               &m,
                               NULL);

        if(r < 0) {
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        sd_bus_error_free(&error);
        sd_bus_message_unref(m);
        return EXIT_SUCCESS;
}

int
pair_device(const char * address)
{
        printf("Method Called: %s\n", __FUNCTION__);
        int r;
        sd_bus_message *m = NULL;

        if(!is_bus_connected()) {
                fprintf(stderr, "Bus is not opened\n");
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        r = sd_bus_call_method(bus,
                               "org.bluez",
                               address,
                               "org.bluez.Device1",
                               "Pair",
                               &error,
                               &m,
                               NULL);

        if(r < 0) {
                fprintf(stderr, "Failed to issue method call: %s\n", error.message);
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        sd_bus_error_free(&error);
        sd_bus_message_unref(m);
        return EXIT_SUCCESS;
}

int
unpair_device(const char * address)
{
        printf("Method Called: %s\n", __FUNCTION__);
        int r;
        sd_bus_message *m = NULL;

        if(!is_bus_connected()) {
                fprintf(stderr, "Bus is not opened\n");
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        r = sd_bus_call_method(bus,
                               "org.bluez",
                               address,
                               "org.bluez.Device1",
                               "CancelPairing",
                               &error,
                               &m,
                               NULL);

        if(r < 0) {
                fprintf(stderr, "Failed to issue method call: %s\n", error.message);
                sd_bus_error_free(&error);
                sd_bus_message_unref(m);
                return EXIT_FAILURE;
        }

        printf("Method Called: %s\n", "CancelPairing");

        sd_bus_error_free(&error);
        sd_bus_message_unref(m);
        return EXIT_SUCCESS;
}

int
write_to_char(const char* address, const char* value)
{
        printf("Method Called: %s\n", __FUNCTION__);
        return EXIT_SUCCESS;
}

int
main(int argc, char *argv[])
{
        open_system_bus();
        list_devices(5);
        int i;
        for(i = 0; i < devices_size; i++) {
                printf("%s\t%s\n", devices[i]->address, devices[i]->name);
        }
        connect_device("/org/bluez/hci0/dev_98_4F_EE_0F_42_B4");
        pair_device("/org/bluez/hci0/dev_98_4F_EE_0F_42_B4");
        unpair_device("/org/bluez/hci0/dev_98_4F_EE_0F_42_B4");
        disconnect_device("/org/bluez/hci0/dev_98_4F_EE_0F_42_B4");
        close_system_bus();
        return 0;
}