#include <FL/Fl.h>
#include <FL/Fl_Window.h>
#include <FL/Fl_Button.h>
#include <FL/Fl_Box.h>
#include <FL/Fl_Wizard.H>
#include <FL/Fl_Choice.H>

#include <windows.h>
#include <vector>
#include <string>

using namespace std;

void boton_callback(Fl_Widget *w, void *data) {
    Fl_Wizard* wizard = static_cast<Fl_Wizard*>(data);
    wizard->next();
}

std::vector<std::string> buscar_puertos_serie() {
    std::vector<std::string> puertos;

    for (int i = 1; i <= 256; i++) {
        std::string nombre = "\\\\.\\COM" + std::to_string(i);

        HANDLE h = CreateFileA(
            nombre.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (h != INVALID_HANDLE_VALUE) {
            puertos.push_back("COM" + std::to_string(i));
            CloseHandle(h);
        }
    }
    return puertos;
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

    const int anchoBoton = 200;
    const int altoBoton = 40;
    const int xBoton = ventana.w()/2-anchoBoton/2;
    const int yBoton = ventana.h()/2;
    Fl_Button boton{xBoton, yBoton, anchoBoton, altoBoton, "Conectar"};
    boton.callback(boton_callback, &wizard);

    const int anchoDesplegable = 100;
    const int altoDesplegable = 25;
    const int xDesplegable = xBoton + anchoDesplegable/2;
    const int yDesplegable = yBoton + (altoBoton/2) + altoDesplegable;
    Fl_Choice desplegableCOM{xDesplegable, yDesplegable, anchoDesplegable, altoDesplegable, "Puerto serie:"};
    auto puertos = buscar_puertos_serie();
    for (auto& p : puertos) {
        desplegableCOM.add(p.c_str());
    }

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


