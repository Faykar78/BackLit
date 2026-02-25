/*
 * main.c - Control Center for Linux
 * A C/GTK4 alternative to Windows Control Center
 */

#include <gtk/gtk.h>
#include "ui.h"

int main(int argc, char *argv[])
{
    GtkApplication *app;
    int status;
    
    app = gtk_application_new("com.colorful.controlcenter", G_APPLICATION_FLAGS_NONE);
    ui_init(app);
    
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}
