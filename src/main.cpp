#include <FL/Fl.h>
#include <FL/Fl_Window.h>
#include <FL/Fl_Button.h>
#include <FL/Fl_Box.h>
#include <FL/Fl_Wizard.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Output.H>

#include <windows.h>
#include <vector>
#include <string>

using namespace std;

string puertoCOM = "";
vector<string> puertosGuardados; // seria mejor que en vez de global, fuese una variable en la misma clase que el desplegable de puertos

std::vector<std::string> buscar_puertos_serie()
{
    std::vector<std::string> puertos;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char valueName[256];
        BYTE data[256];
        DWORD valueNameSize, dataSize, type, index = 0;

        while (true)
        {
            valueNameSize = sizeof(valueName);
            dataSize = sizeof(data);
            LONG ret = RegEnumValueA(hKey, index, valueName, &valueNameSize, nullptr, &type, data, &dataSize);
            if (ret != ERROR_SUCCESS)
                break;

            if (type == REG_SZ)
                puertos.push_back(std::string(reinterpret_cast<char *>(data)));

            ++index;
        }
        RegCloseKey(hKey);
    }
    return puertos;
}

void botonConectar_callback(Fl_Widget *w, void *data)
{
    Fl_Wizard *wizard = static_cast<Fl_Wizard *>(data);
    wizard->next();
}

void desplegable_callback(Fl_Widget *w, void *data)
{
    Fl_Choice *pDesplegable = static_cast<Fl_Choice *>(w);
    Fl_Output *pTextoCOM = static_cast<Fl_Output *>(data); // pequeño texto para observar la variable global del puerto elegido

    int indice = pDesplegable->value();
    if (indice == -1)
    {
        // Nada seleccionado
        return;
    }

    string puertoSeleccionado = pDesplegable->mvalue()->label(); // mvalue() devuelve el objeto menu item seleccionado. value() solo devuelve un entero del indice seleccionado
    pTextoCOM->value(puertoSeleccionado.c_str());
    puertoCOM = puertoSeleccionado;
}

void botonActualizar_callback(Fl_Widget *w, void *data)
{
    Fl_Choice *pDesplegable = static_cast<Fl_Choice *>(data);
    pDesplegable->clear();
    puertosGuardados = buscar_puertos_serie(); // puertosGuardados tiene que ser global porque el metodo add() recibe punteros de cada string en puertosGuardados. Al terminar la funcion callback, si puertosGuardados fuera local, se destruye la variable y los punteros apuntan a memoria rara (errores, comportamiento inesperado...)
    for (const string &p : puertosGuardados)
    {
        pDesplegable->add(p.c_str());
    }
    pDesplegable->value(-1);
    pDesplegable->redraw();
    puertoCOM = "";
}

int main()
{
    Fl_Window ventana(0, 0, 600, 338, "Control de temperatura - Alberto Serrano Moreno");
    Fl_Wizard wizard{0, 0, ventana.w(), ventana.h()};

    /* =======================
       PANTALLA(GRUPO) BIENVENIDA
       ======================= */
    Fl_Group grupoBienvenida(0, 0, ventana.w(), ventana.h());

    // TITULO
    Fl_Box tituloBienvenida(0, ventana.h() / 4, ventana.w(), 40, "Bienvenido");
    tituloBienvenida.labelsize(40);

    // BOTON CONECTAR
    const int wBotonConectar = 200;
    const int hBotonConectar = 40;
    const int xBotonConectar = ventana.w() / 2 - wBotonConectar / 2;
    const int yBotonConectar = ventana.h() / 2;
    Fl_Button botonConectar{xBotonConectar, yBotonConectar, wBotonConectar, hBotonConectar, "Conectar"};
    botonConectar.callback(botonConectar_callback, &wizard);

    // TEXTO PUERTO SERIE ELEGIDO (TEST)
    Fl_Output textoCOM{200, 0, 100, 40, "COM ELEGIDO: "};

    // DESPLEGABLE PUERTOS SERIE
    const int wDesplegable = wBotonConectar / 2;
    const int hDesplegable = 25;
    const int xDesplegable = xBotonConectar;
    const int yDesplegable = yBotonConectar + (hBotonConectar / 2) + hDesplegable;
    Fl_Choice desplegableCOM{xDesplegable, yDesplegable, wDesplegable, hDesplegable, "Puerto serie:"};
    desplegableCOM.callback(desplegable_callback, &textoCOM);

    // BOTON ACTUALIZAR PUERTOS SERIE DEL DESPLEGABLE
    const int wBotonActualizar = wBotonConectar / 2;
    const int hBotonActualizar = hDesplegable;
    const int xBotonActualizar = xDesplegable + wDesplegable;
    const int yBotonActualizar = yDesplegable;
    Fl_Button botonActualizar{xBotonActualizar, yBotonActualizar, wBotonActualizar, hBotonActualizar, "Actualizar"};
    botonActualizar.callback(botonActualizar_callback, &desplegableCOM);

    grupoBienvenida.end();

    /* =======================
       PANTALLA(GRUPO) PRINCIPAL
       ======================= */
    Fl_Group grupoPrincipal(0, 0, ventana.w(), ventana.h());

    // TITULO
    Fl_Box titulo(0, 0, ventana.w(), 40, "Control de temperatura STM32");
    titulo.labelsize(24);

    grupoPrincipal.end();

    wizard.end();
    ventana.end();

    // wizard.value(&grupoPrincipal); // Página inicial

    ventana.show();
    return Fl::run();
}
