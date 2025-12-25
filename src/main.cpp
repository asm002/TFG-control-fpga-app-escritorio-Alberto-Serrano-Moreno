#include <FL/Fl.h>
#include <FL/Fl_Window.h>
#include <FL/Fl_Button.h>
#include <FL/Fl_Box.h>
#include <FL/Fl_Wizard.H>

void boton_callback(Fl_Widget *w, void *data) {
    Fl_Wizard* wizard = static_cast<Fl_Wizard*>(data);
    wizard->next();
}

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

    const int ancho = 200;
    Fl_Button boton{ventana.w()/2-ancho/2, ventana.h()/2, ancho, 20, "Menú Principal"};
    boton.callback(boton_callback, &wizard);

    grupoBienvenida.end();


    /* =======================
       PANTALLA(GRUPO) PRINCIPAL
       ======================= */
    Fl_Group grupoPrincipal(0, 0, ventana.w(), ventana.h());

    Fl_Box titulo(0, 0, ventana.w(), 40, "Control de temperatura STM32");
    titulo.labelsize(24);

    grupoPrincipal.end();



    wizard.end();
    ventana.end();

    //wizard.value(&grupoPrincipal); // Página inicial

    ventana.show();
    return Fl::run();
}


