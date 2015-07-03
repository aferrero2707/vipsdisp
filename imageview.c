/* Display an image with gtk3 and libvips. 
 */

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include <vips/vips.h>

#include "disp.h"

G_DEFINE_TYPE( Imageview, imageview, GTK_TYPE_APPLICATION_WINDOW );

static void
imageview_init( Imageview *Imageview )
{
	printf( "imageview_init:\n" ); 
}

static void
imageview_class_init( ImageviewClass *class )
{
	printf( "imageview_class_init:\n" ); 
}

static void
imageview_magin( GSimpleAction *action, 
	GVariant *parameter, gpointer user_data )
{
	Imageview *imageview = (Imageview *) user_data;

	imagepresent_magin( imageview->imagepresent ); 
}

static void
imageview_magout( GSimpleAction *action, 
	GVariant *parameter, gpointer user_data )
{
	Imageview *imageview = (Imageview *) user_data;

	imagepresent_magout( imageview->imagepresent ); 
}

static void
imageview_normal( GSimpleAction *action, 
	GVariant *parameter, gpointer user_data )
{
	Imageview *imageview = (Imageview *) user_data;

	imagepresent_set_mag( imageview->imagepresent, 1 );
}

static void
imageview_bestfit( GSimpleAction *action, 
	GVariant *parameter, gpointer user_data )
{
	Imageview *imageview = (Imageview *) user_data;

	imagepresent_bestfit( imageview->imagepresent );
}

static GActionEntry imageview_entries[] = {
	{ "magin", imageview_magin },
	{ "magout", imageview_magout },
	{ "normal", imageview_normal },
	{ "bestfit", imageview_bestfit }
};

static int
imageview_update_header( Imageview *imageview )
{
	char *path;

	if( (path = imagepresent_get_path( imageview->imagepresent )) ) { 
		char *basename;

		basename = g_path_get_basename( path );
		g_free( path ); 
		gtk_header_bar_set_title( 
			GTK_HEADER_BAR( imageview->header_bar ), basename );
		g_free( basename ); 
	}
	else
		gtk_header_bar_set_title( 
			GTK_HEADER_BAR( imageview->header_bar ), 
			"Untitled" );

	return( 0 );
}

static void
imageview_open_clicked( GtkWidget *button, Imageview *imageview )
{
	GtkWidget *dialog;
	char *path;
	int result;

	dialog = gtk_file_chooser_dialog_new( "Select a file",
		GTK_WINDOW( imageview ) , 
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel", GTK_RESPONSE_CANCEL,
		"_Open", GTK_RESPONSE_ACCEPT,
		NULL );

	if( (path = imagepresent_get_path( imageview->imagepresent )) ) {
		gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( dialog ),
			path );
		g_free( path ); 
	}

	result = gtk_dialog_run( GTK_DIALOG( dialog ) );
	if( result == GTK_RESPONSE_ACCEPT ) {
		char *path;
		GFile *file;

		path = gtk_file_chooser_get_filename( 
			GTK_FILE_CHOOSER( dialog ) );
		file = g_file_new_for_path( path );
		g_free( path );
		imagepresent_set_file( imageview->imagepresent, file ); 
		g_object_unref( file ); 

		imageview_update_header( imageview ); 
	}

	gtk_widget_destroy( dialog );
}

Imageview *
imageview_new( GtkApplication *application, GFile *file )
{
	Disp *disp = (Disp *) application;

	Imageview *imageview;
	GtkWidget *open;
	GtkWidget *menu_button;
	GtkBuilder *builder;
	GMenuModel *menu;
	int width;
	int height;

	printf( "imageview_new: file = %p\n", file ); 

	imageview = g_object_new( imageview_get_type(),
		"application", application,
		NULL );
	g_action_map_add_action_entries( G_ACTION_MAP( imageview ), 
		imageview_entries, G_N_ELEMENTS( imageview_entries ), 
		imageview );

	imageview->disp = disp;

	imageview->header_bar = gtk_header_bar_new(); 

	gtk_header_bar_set_show_close_button( 
		GTK_HEADER_BAR( imageview->header_bar ), TRUE );

	open = gtk_button_new_with_label( "Open" );
	gtk_header_bar_pack_start( 
		GTK_HEADER_BAR( imageview->header_bar ), open ); 
	g_signal_connect( open, "clicked", 
		G_CALLBACK( imageview_open_clicked ), imageview );

	menu_button = gtk_menu_button_new();
	gtk_header_bar_pack_end( 
		GTK_HEADER_BAR( imageview->header_bar ), menu_button ); 
	builder = gtk_builder_new_from_resource( 
		"/vips/disp/gtk/imageview-popover.ui" ); 
	menu = G_MENU_MODEL( gtk_builder_get_object( builder, 
		"imageview-popover-menu" ) );
	gtk_menu_button_set_menu_model( GTK_MENU_BUTTON( menu_button ), menu );
	g_object_unref( builder );

	gtk_window_set_titlebar( GTK_WINDOW( imageview ), 
		imageview->header_bar ); 

	imageview->imagepresent = imagepresent_new();
	gtk_widget_set_hexpand( GTK_WIDGET( imageview->imagepresent ), TRUE ); 
	gtk_widget_set_vexpand( GTK_WIDGET( imageview->imagepresent ), TRUE ); 
	gtk_container_add( GTK_CONTAINER( imageview ), 
		GTK_WIDGET( imageview->imagepresent ) );

	imagepresent_set_file( imageview->imagepresent, file ); 

	/* 83 is a magic number for the height of the top 
	 * bar on my laptop. 
	 */
	if( imagepresent_get_image_size( imageview->imagepresent, 
		&width, &height ) )  
		gtk_window_set_default_size( GTK_WINDOW( imageview ), 
			VIPS_MIN( 800, width ),
			VIPS_MIN( 800, height + 83 ) ); 

	gtk_widget_show_all( GTK_WIDGET( imageview ) );

	return( imageview ); 
}