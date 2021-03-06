
typedef struct _Imageview {
	GtkApplicationWindow parent_instance;

	Disp *disp;

	GtkWidget *header_bar;

	GtkWidget *progress_info;
	GtkWidget *progress;

	GtkWidget *error_info;
	GtkWidget *error_label;

	Imagepresent *imagepresent;

	GtkWidget *status_bar;
	GtkWidget *info_label;
	GtkWidget *coord_label;
	GtkWidget *value_label;
	GtkWidget *mag_label;

} Imageview;

typedef GtkApplicationWindowClass ImageviewClass;

void imageview_status_update( Imageview *imageview );

Imageview *imageview_new( GtkApplication *application, GFile *file );
