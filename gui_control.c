#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>

#define DEVICE_PATH "/dev/vled"
#define SYSFS_STATE "/sys/class/vled/vled/led_state"
#define SYSFS_BRIGHTNESS "/sys/class/vled/vled/brightness"
#define SYSFS_COLOR "/sys/class/vled/vled/color"

// Глобальные переменные для состояния светодиода
typedef struct {
    gboolean led_state;
    gint brightness;
    gchar color[20];
    cairo_surface_t *led_on_surface;
    cairo_surface_t *led_off_surface;
} LedState;

static LedState led_state = {
    .led_state = FALSE,
    .brightness = 128,
    .color = "green",
    .led_on_surface = NULL,
    .led_off_surface = NULL
};

// Глобальные виджеты
static GtkWidget *led_indicator = NULL;
static GtkWidget *brightness_scale = NULL;
static GtkWidget *color_combo = NULL;
static GtkWidget *status_label = NULL;
static GtkWidget *log_textview = NULL;
static GtkTextBuffer *log_buffer = NULL;
static GtkWidget *toggle_button = NULL;

// Логирование в графическом интерфейсе
static void gui_log(const char *message)
{
    if (!log_buffer) return;
    
    GtkTextIter end;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
    
    char *log_message = g_strdup_printf("[%s] %s\n", timestamp, message);
    
    gtk_text_buffer_get_end_iter(log_buffer, &end);
    gtk_text_buffer_insert(log_buffer, &end, log_message, -1);
    
    // Автопрокрутка к новому сообщению
    GtkTextMark *mark = gtk_text_buffer_create_mark(log_buffer, "end", &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(log_textview), mark, 0.0, FALSE, 0.0, 0.0);
    gtk_text_buffer_delete_mark(log_buffer, mark);
    
    g_free(log_message);
}

// Функции для работы с драйвером
static void write_to_device(const char *command)
{
    int fd = open(DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Failed to open device: %s", strerror(errno));
        gui_log(error_msg);
        return;
    }
    
    ssize_t bytes_written = write(fd, command, strlen(command));
    if (bytes_written < 0) {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Write failed: %s", strerror(errno));
        gui_log(error_msg);
    } else {
        char log_msg[100];
        snprintf(log_msg, sizeof(log_msg), "Sent command: %s", command);
        gui_log(log_msg);
    }
    
    close(fd);
}

static void write_to_sysfs(const char *path, const char *value)
{
    FILE *fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%s", value);
        fclose(fp);
        char log_msg[100];
        snprintf(log_msg, sizeof(log_msg), "Sysfs write: %s = %s", path, value);
        gui_log(log_msg);
    } else {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Failed to open %s: %s", path, strerror(errno));
        gui_log(error_msg);
    }
}

static gboolean read_from_sysfs(const char *path, char *buffer, size_t size)
{
    FILE *fp = fopen(path, "r");
    if (fp) {
        if (fgets(buffer, size, fp) == NULL) {
            strcpy(buffer, "Error");
            fclose(fp);
            return FALSE;
        }
        fclose(fp);
        
        // Удаляем символ новой строки
        buffer[strcspn(buffer, "\n")] = 0;
        return TRUE;
    } else {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Failed to read %s: %s", path, strerror(errno));
        gui_log(error_msg);
        return FALSE;
    }
}

// Создание изображения светодиода
static cairo_surface_t* create_led_surface(gboolean on, const char *color_name)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 200);
    cairo_t *cr = cairo_create(surface);
    
    // Цвета в зависимости от выбранного цвета
    double r, g, b;
    
    if (strcmp(color_name, "red") == 0) {
        r = 1.0; g = 0.2; b = 0.2;
    } else if (strcmp(color_name, "green") == 0) {
        r = 0.2; g = 1.0; b = 0.2;
    } else if (strcmp(color_name, "blue") == 0) {
        r = 0.2; g = 0.2; b = 1.0;
    } else if (strcmp(color_name, "yellow") == 0) {
        r = 1.0; g = 1.0; b = 0.2;
    } else if (strcmp(color_name, "white") == 0) {
        r = 1.0; g = 1.0; b = 1.0;
    } else if (strcmp(color_name, "cyan") == 0) {
        r = 0.2; g = 1.0; b = 1.0;
    } else if (strcmp(color_name, "magenta") == 0) {
        r = 1.0; g = 0.2; b = 1.0;
    } else {
        r = 0.2; g = 1.0; b = 0.2; // По умолчанию зеленый
    }
    
    // Регулировка яркости
    double brightness_factor = led_state.brightness / 255.0;
    if (!on) {
        brightness_factor *= 0.3; // Для выключенного состояния
    }
    
    // Центр и радиус
    double center_x = 100.0;
    double center_y = 100.0;
    double radius = 80.0;
    
    if (on) {
        // Включенный светодиод - градиент с подсветкой
        cairo_pattern_t *pat = cairo_pattern_create_radial(
            center_x - 30, center_y - 30, 10,
            center_x, center_y, radius
        );
        
        // Яркое ядро
        cairo_pattern_add_color_stop_rgba(pat, 0.0,
            r * brightness_factor * 1.5,
            g * brightness_factor * 1.5,
            b * brightness_factor * 1.5,
            1.0);
        
        // Основной цвет
        cairo_pattern_add_color_stop_rgba(pat, 0.3,
            r * brightness_factor,
            g * brightness_factor,
            b * brightness_factor,
            0.9);
        
        // Края с затуханием
        cairo_pattern_add_color_stop_rgba(pat, 1.0,
            r * brightness_factor * 0.3,
            g * brightness_factor * 0.3,
            b * brightness_factor * 0.3,
            0.3);
        
        cairo_set_source(cr, pat);
        cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
        
        // Блик
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);
        cairo_arc(cr, center_x - 25, center_y - 25, 15, 0, 2 * M_PI);
        cairo_fill(cr);
        
        // Эффект свечения
        cairo_set_source_rgba(cr, r, g, b, 0.2);
        cairo_arc(cr, center_x, center_y, radius + 5, 0, 2 * M_PI);
        cairo_fill(cr);
        
    } else {
        // Выключенный светодиод - плоский серый
        cairo_pattern_t *pat = cairo_pattern_create_radial(
            center_x - 20, center_y - 20, 5,
            center_x, center_y, radius
        );
        
        cairo_pattern_add_color_stop_rgba(pat, 0.0, 0.8, 0.8, 0.8, 1.0);
        cairo_pattern_add_color_stop_rgba(pat, 0.5, 0.5, 0.5, 0.5, 0.9);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.3, 0.3, 0.3, 0.7);
        
        cairo_set_source(cr, pat);
        cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    }
    
    // Обводка
    cairo_set_line_width(cr, 3.0);
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1.0);
    cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
    cairo_stroke(cr);
    
    // Контактные ножки
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_rectangle(cr, center_x - 15, center_y + radius, 30, 20);
    cairo_fill(cr);
    cairo_rectangle(cr, center_x - 15, center_y - radius - 20, 30, 20);
    cairo_fill(cr);
    
    cairo_destroy(cr);
    return surface;
}

// Обновление изображения светодиода
static void update_led_image(void)
{
    if (led_state.led_on_surface) {
        cairo_surface_destroy(led_state.led_on_surface);
    }
    if (led_state.led_off_surface) {
        cairo_surface_destroy(led_state.led_off_surface);
    }
    
    led_state.led_on_surface = create_led_surface(TRUE, led_state.color);
    led_state.led_off_surface = create_led_surface(FALSE, led_state.color);
    
    if (led_indicator) {
        gtk_widget_queue_draw(led_indicator);
    }
}

// Функция для отрисовки светодиода
static gboolean draw_led(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    int size = (width < height) ? width : height;
    
    cairo_surface_t *surface;
    if (led_state.led_state) {
        surface = led_state.led_on_surface;
    } else {
        surface = led_state.led_off_surface;
    }
    
    if (surface) {
        // Вычисляем масштаб
        double scale = (double)size / 200.0;
        
        // Сохраняем текущую матрицу трансформации
        cairo_save(cr);
        
        // Центрируем изображение
        cairo_translate(cr, (width - size) / 2.0, (height - size) / 2.0);
        cairo_scale(cr, scale, scale);
        
        // Рисуем изображение
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        
        // Восстанавливаем матрицу
        cairo_restore(cr);
    } else {
        // Резервная отрисовка
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_arc(cr, width/2, height/2, size/2 - 10, 0, 2 * M_PI);
        cairo_fill(cr);
        
        cairo_set_line_width(cr, 2);
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
        cairo_arc(cr, width/2, height/2, size/2 - 10, 0, 2 * M_PI);
        cairo_stroke(cr);
    }
    
    return FALSE;
}

// Обработчики событий
static void on_toggle_led(GtkWidget *widget, gpointer data)
{
    gboolean active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    led_state.led_state = active;
    
    if (active) {
        write_to_device("ON");
        write_to_sysfs(SYSFS_STATE, "1");
        gtk_label_set_text(GTK_LABEL(status_label), "LED: ON");
        gui_log("LED turned ON");
    } else {
        write_to_device("OFF");
        write_to_sysfs(SYSFS_STATE, "0");
        gtk_label_set_text(GTK_LABEL(status_label), "LED: OFF");
        gui_log("LED turned OFF");
    }
    
    update_led_image();
}

static void on_brightness_changed(GtkRange *range, gpointer data)
{
    int brightness = (int)gtk_range_get_value(range);
    led_state.brightness = brightness;
    
    char cmd[50];
    sprintf(cmd, "BRIGHTNESS %d", brightness);
    write_to_device(cmd);
    
    char value[10];
    sprintf(value, "%d", brightness);
    write_to_sysfs(SYSFS_BRIGHTNESS, value);
    
    char status[50];
    sprintf(status, "Brightness: %d", brightness);
    gtk_label_set_text(GTK_LABEL(status_label), status);
    
    char log_msg[50];
    sprintf(log_msg, "Brightness set to %d", brightness);
    gui_log(log_msg);
    
    update_led_image();
}

static void on_color_changed(GtkComboBox *combo, gpointer data)
{
    gchar *color = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (color) {
        strncpy(led_state.color, color, sizeof(led_state.color) - 1);
        led_state.color[sizeof(led_state.color) - 1] = '\0';
        
        char cmd[50];
        sprintf(cmd, "COLOR %s", color);
        write_to_device(cmd);
        write_to_sysfs(SYSFS_COLOR, color);
        
        char status[50];
        sprintf(status, "Color: %s", color);
        gtk_label_set_text(GTK_LABEL(status_label), status);
        
        char log_msg[50];
        sprintf(log_msg, "Color changed to %s", color);
        gui_log(log_msg);
        
        update_led_image();
        g_free(color);
    }
}

static void on_read_state(GtkWidget *widget, gpointer data)
{
    char state_buf[20], brightness_buf[20], color_buf[20];
    
    if (read_from_sysfs(SYSFS_STATE, state_buf, sizeof(state_buf)) &&
        read_from_sysfs(SYSFS_BRIGHTNESS, brightness_buf, sizeof(brightness_buf)) &&
        read_from_sysfs(SYSFS_COLOR, color_buf, sizeof(color_buf))) {
        
        // Обновляем глобальное состояние
        led_state.led_state = atoi(state_buf);
        led_state.brightness = atoi(brightness_buf);
        strncpy(led_state.color, color_buf, sizeof(led_state.color) - 1);
        
        // Обновляем UI
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_button), led_state.led_state);
        gtk_range_set_value(GTK_RANGE(brightness_scale), led_state.brightness);
        
        // Устанавливаем цвет в комбобоксе
        GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(color_combo);
        GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
        int index = 0;
        
        while (valid) {
            gchar *item_text;
            gtk_tree_model_get(model, &iter, 0, &item_text, -1);
            if (strcmp(item_text, led_state.color) == 0) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
                g_free(item_text);
                break;
            }
            g_free(item_text);
            valid = gtk_tree_model_iter_next(model, &iter);
            index++;
        }
        
        char status[100];
        sprintf(status, "State: %s, Brightness: %s, Color: %s", 
                state_buf, brightness_buf, color_buf);
        gtk_label_set_text(GTK_LABEL(status_label), status);
        
        update_led_image();
        gui_log("State read from driver");
    }
}

static void on_refresh(GtkWidget *widget, gpointer data)
{
    on_read_state(widget, data);
}

static void on_clear_log(GtkWidget *widget, gpointer data)
{
    gtk_text_buffer_set_text(log_buffer, "", -1);
    gui_log("Log cleared");
}

static void on_about(GtkWidget *widget, gpointer data)
{
    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Virtual LED Controller");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "2.1");
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), "(c) 2025 Alexander Shelestov");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog), 
        "Graphical interface for Virtual LED Driver\n"
        "Controls virtual LED device through character device and sysfs interfaces");
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_GPL_3_0);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void check_driver_availability(void)
{
    if (access(DEVICE_PATH, F_OK) != 0) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "Driver not loaded!\n\nPlease load the driver first:\n"
            "sudo make install\n\n"
            "Or manually:\n"
            "sudo insmod virtual_led_driver.ko");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        gui_log("ERROR: Driver not loaded. Please install the driver module.");
    } else {
        gui_log("Driver found. Reading initial state...");
        on_read_state(NULL, NULL);
        gui_log("Application started successfully.");
    }
}

int main(int argc, char *argv[])
{
    GtkWidget *window;
    GtkWidget *vbox, *hbox, *frame;
    GtkWidget *read_button, *refresh_button, *clear_button;
    GtkWidget *brightness_label;
    GtkWidget *color_label;
    GtkWidget *drawing_area;
    GtkWidget *scrolled_window;
    GtkWidget *menu_bar, *menu, *menu_item;
    
    gtk_init(&argc, &argv);
    
    // Создание главного окна
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Virtual LED Controller v2.1");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 800);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    
    // Главный контейнер
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    // Меню
    menu_bar = gtk_menu_bar_new();
    menu = gtk_menu_new();
    menu_item = gtk_menu_item_new_with_label("File");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), menu);
    
    GtkWidget *about_item = gtk_menu_item_new_with_label("About");
    g_signal_connect(about_item, "activate", G_CALLBACK(on_about), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about_item);
    
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), menu_item);
    gtk_box_pack_start(GTK_BOX(vbox), menu_bar, FALSE, FALSE, 0);
    
    // Область отрисовки светодиода
    frame = gtk_frame_new("LED Indicator");
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 10);
    
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, 250, 250);
    gtk_container_add(GTK_CONTAINER(frame), drawing_area);
    led_indicator = drawing_area;
    
    // Кнопка переключения
    toggle_button = gtk_toggle_button_new_with_label("Turn LED ON/OFF");
    gtk_button_set_relief(GTK_BUTTON(toggle_button), GTK_RELIEF_NORMAL);
    gtk_widget_set_margin_top(toggle_button, 10);
    gtk_widget_set_margin_bottom(toggle_button, 10);
    gtk_box_pack_start(GTK_BOX(vbox), toggle_button, FALSE, FALSE, 0);
    g_signal_connect(toggle_button, "toggled", G_CALLBACK(on_toggle_led), NULL);
    
    // Регулировка яркости
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(hbox, 5);
    gtk_widget_set_margin_bottom(hbox, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    brightness_label = gtk_label_new("Brightness:");
    gtk_widget_set_size_request(brightness_label, 80, -1);
    gtk_box_pack_start(GTK_BOX(hbox), brightness_label, FALSE, FALSE, 0);
    
    brightness_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 255, 1);
    gtk_range_set_value(GTK_RANGE(brightness_scale), 128);
    gtk_scale_set_digits(GTK_SCALE(brightness_scale), 0);
    gtk_scale_set_draw_value(GTK_SCALE(brightness_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(brightness_scale), GTK_POS_RIGHT);
    gtk_widget_set_size_request(brightness_scale, 350, -1);
    gtk_box_pack_start(GTK_BOX(hbox), brightness_scale, TRUE, TRUE, 0);
    g_signal_connect(brightness_scale, "value-changed", G_CALLBACK(on_brightness_changed), NULL);
    
    // Выбор цвета
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(hbox, 5);
    gtk_widget_set_margin_bottom(hbox, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    color_label = gtk_label_new("Color:");
    gtk_widget_set_size_request(color_label, 80, -1);
    gtk_box_pack_start(GTK_BOX(hbox), color_label, FALSE, FALSE, 0);
    
    color_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "red");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "green");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "blue");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "yellow");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "white");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "cyan");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "magenta");
    gtk_combo_box_set_active(GTK_COMBO_BOX(color_combo), 1);
    gtk_widget_set_size_request(color_combo, 150, -1);
    gtk_box_pack_start(GTK_BOX(hbox), color_combo, FALSE, FALSE, 0);
    g_signal_connect(color_combo, "changed", G_CALLBACK(on_color_changed), NULL);
    
    // Панель кнопок управления
    hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_CENTER);
    gtk_box_set_spacing(GTK_BOX(hbox), 10);
    gtk_widget_set_margin_top(hbox, 10);
    gtk_widget_set_margin_bottom(hbox, 10);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);
    
    read_button = gtk_button_new_with_label("Read State");
    gtk_widget_set_tooltip_text(read_button, "Read current state from driver");
    gtk_container_add(GTK_CONTAINER(hbox), read_button);
    g_signal_connect(read_button, "clicked", G_CALLBACK(on_read_state), NULL);
    
    refresh_button = gtk_button_new_with_label("Refresh");
    gtk_widget_set_tooltip_text(refresh_button, "Refresh display");
    gtk_container_add(GTK_CONTAINER(hbox), refresh_button);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh), NULL);
    
    clear_button = gtk_button_new_with_label("Clear Log");
    gtk_widget_set_tooltip_text(clear_button, "Clear event log");
    gtk_container_add(GTK_CONTAINER(hbox), clear_button);
    g_signal_connect(clear_button, "clicked", G_CALLBACK(on_clear_log), NULL);
    
    // Статусная строка
    frame = gtk_frame_new("Status");
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 5);
    
    status_label = gtk_label_new("Ready. Connect to driver first.");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0);
    gtk_widget_set_margin_start(status_label, 5);
    gtk_widget_set_margin_end(status_label, 5);
    gtk_widget_set_margin_top(status_label, 5);
    gtk_widget_set_margin_bottom(status_label, 5);
    gtk_container_add(GTK_CONTAINER(frame), status_label);
    
    // Лог
    frame = gtk_frame_new("Event Log");
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 5);
    
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled_window), 150);
    gtk_container_add(GTK_CONTAINER(frame), scrolled_window);
    
    log_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_textview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_textview), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_textview), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled_window), log_textview);
    
    log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_textview));
    
    // Обработчик закрытия окна
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // Обработчик отрисовки светодиода
    g_signal_connect(G_OBJECT(drawing_area), "draw", G_CALLBACK(draw_led), NULL);
    
    // Инициализация изображений светодиода
    update_led_image();
    
    gtk_widget_show_all(window);
    
    // Запускаем проверку драйвера после отображения окна
    g_idle_add((GSourceFunc)check_driver_availability, NULL);
    
    gtk_main();
    
    // Очистка ресурсов
    if (led_state.led_on_surface)
        cairo_surface_destroy(led_state.led_on_surface);
    if (led_state.led_off_surface)
        cairo_surface_destroy(led_state.led_off_surface);
    
    return 0;
}