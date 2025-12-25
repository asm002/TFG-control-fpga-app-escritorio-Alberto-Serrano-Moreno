#include <FL/Fl.h>
#include <FL/Fl_Window.h>
#include <FL/Fl_Button.h>
#include <FL/Fl_Box.h>
#include <FL/Fl_Wizard.H>

int main()
{
    Fl_Window ventana(0, 0, 600, 338, "Control de temperatura - Alberto Serrano Moreno");
    Fl_Wizard wizard{0, 0, ventana.w(), ventana.h()};

    /* =======================
       PANTALLA(GRUPO) BIENVENIDA
       ======================= */
    Fl_Group grupoBienvenida(0, 0, ventana.w(), ventana.h());

    Fl_Box tituloBienvenida(0, 0, ventana.w(), 40, "Bienvenido");
    tituloBienvenida.labelsize(24);

    grupoBienvenida.end();

    wizard.end();
    wizard.value(&grupoBienvenida); // Página inicial

    ventana.show();
    return Fl::run();
}
