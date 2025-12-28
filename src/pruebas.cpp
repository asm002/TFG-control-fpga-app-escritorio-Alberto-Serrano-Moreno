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

// Función para buscar puertos serie (igual que antes)
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

// =========================
// CLASE PANTALLA BIENVENIDA
// =========================
class PantallaBienvenida
{
public:
    Fl_Group grupo;
    Fl_Box tituloBienvenida;
    Fl_Button botonConectar;
    Fl_Choice desplegableCOM;
    Fl_Button botonActualizar;
    Fl_Output textoCOM;

    std::string puertoCOM;
    vector<string> puertosGuardados;

    PantallaBienvenida(int w, int h, Fl_Wizard *wizard)
        : grupo(0, 0, w, h),
          tituloBienvenida(0, h / 4, w, 40, "Bienvenido"),
          botonConectar(0, 0, 0, 0, "Conectar"),
          desplegableCOM(0, 0, 0, 0, "Puerto serie:"),
          botonActualizar(0, 0, 0, 0, "Actualizar"),
          textoCOM(200, 0, 100, 40, "COM ELEGIDO:")
    {
        // Posicionamiento
        const int wBotonConectar = 200;
        const int hBotonConectar = 40;
        const int xBotonConectar = w / 2 - wBotonConectar / 2;
        const int yBotonConectar = h / 2;
        botonConectar.resize(xBotonConectar, yBotonConectar, wBotonConectar, hBotonConectar);
        botonConectar.callback(botonConectar_cb, wizard);

        const int wDesplegable = wBotonConectar / 2;
        const int hDesplegable = 25;
        const int xDesplegable = xBotonConectar;
        const int yDesplegable = yBotonConectar + (hBotonConectar / 2) + hDesplegable;
        desplegableCOM.resize(xDesplegable, yDesplegable, wDesplegable, hDesplegable);
        desplegableCOM.callback(desplegable_cb, this);

        const int wBotonActualizar = wBotonConectar / 2;
        const int xBotonActualizar = xDesplegable + wDesplegable;
        const int yBotonActualizar = yDesplegable;
        botonActualizar.resize(xBotonActualizar, yBotonActualizar, wBotonActualizar, hDesplegable);
        botonActualizar.callback(botonActualizar_cb, this);

        grupo.end();
    }

private:
    // Callback estático para botón Conectar
    static void botonConectar_cb(Fl_Widget *w, void *data)
    {
        Fl_Wizard *wizard = static_cast<Fl_Wizard *>(data);
        wizard->next();
    }

    // Callback estático para desplegable
    static void desplegable_cb(Fl_Widget *w, void *data)
    {
        PantallaBienvenida *p = static_cast<PantallaBienvenida *>(data);
        int indice = p->desplegableCOM.value();
        if (indice == -1)
            return;
        string puertoSeleccionado = p->desplegableCOM.mvalue()->label();
        p->textoCOM.value(puertoSeleccionado.c_str());
        p->puertoCOM = puertoSeleccionado;
    }

    // Callback estático para botón Actualizar
    static void botonActualizar_cb(Fl_Widget *w, void *data)
    {
        PantallaBienvenida *p = static_cast<PantallaBienvenida *>(data);
        p->desplegableCOM.clear();
        p->puertosGuardados = buscar_puertos_serie();
        for (const auto &puerto : p->puertosGuardados)
            p->desplegableCOM.add(puerto.c_str());
        p->desplegableCOM.value(-1);
        p->desplegableCOM.redraw();
        p->puertoCOM = "";
    }
};

// =========================
// CLASE PANTALLA PRINCIPAL
// =========================
class PantallaPrincipal
{
public:
    Fl_Group grupo;
    Fl_Box titulo;

    PantallaPrincipal(int w, int h)
        : grupo(0, 0, w, h),
          titulo(0, 0, w, 40, "Control de temperatura STM32")
    {
        titulo.labelsize(24);
        grupo.end();
    }
};

// =========================
// MAIN
// =========================
int main()
{
    Fl_Window ventana(0, 0, 600, 338, "Control de temperatura - Alberto Serrano Moreno");
    Fl_Wizard wizard{0, 0, ventana.w(), ventana.h()};

    // Instanciamos pantallas
    PantallaBienvenida pantallaBienvenida{ventana.w(), ventana.h(), &wizard};
    PantallaPrincipal pantallaPrincipal{ventana.w(), ventana.h()};

    wizard.end();
    ventana.end();

    // wizard.value(&pantallaPrincipal.grupo); // si quieres que inicie en la pantalla principal

    ventana.show();
    return Fl::run();
}
