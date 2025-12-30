#include <FL/Fl.h>
#include <FL/Fl_Window.h>
#include <FL/Fl_Button.h>
#include <FL/Fl_Box.h>
#include <FL/Fl_Wizard.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Output.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Hor_Value_Slider.H>

#include <windows.h>
#include <vector>
#include <string>

#include <stdexcept>

#include <memory>

#include <windows.h>

using namespace std;

string puertoString = "";
vector<string> puertosGuardados; // seria mejor que en vez de global, fuese una variable en la misma clase que el desplegable de puertos
class SerialPort;
std::unique_ptr<SerialPort> puertoSerie; // puntero inteligente global para el objeto puerto serie, que se construye en el callback de boton conectar pero el objeto no vive en el ambito del callback (porque sino, se destruiria al finalizar el callback)

class SerialPort
{
public:
    SerialPort(const std::string &portName)
    {
        _serialHandle = CreateFileA(("\\\\.\\" + portName).c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

        if (_serialHandle == INVALID_HANDLE_VALUE)
        {
            // CloseHandle(_serialHandle);   //no, segun chatgpt
            throw std::runtime_error("No se pudo abrir el puerto serie.");
        }

        DCB serialParams{};
        serialParams.BaudRate = CBR_9600;
        serialParams.ByteSize = 8;
        serialParams.StopBits = ONESTOPBIT;
        serialParams.Parity = NOPARITY;
        serialParams.DCBlength = sizeof(serialParams);

        if (!SetCommState(_serialHandle, &serialParams))
        {
            CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo configurar el puerto serie.");
        }

        COMMTIMEOUTS t{};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutConstant = 0;
        t.ReadTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(_serialHandle, &t))
        {
            CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo configurar los timeouts del puerto serie.");
        }
    }

    ~SerialPort()
    {
        CloseHandle(_serialHandle);
    }

    void send(int value)
    {
        std::string data = std::to_string(value) + "\n";
        DWORD bytesWritten{0};

        if (!WriteFile(_serialHandle, data.c_str(), data.size(), &bytesWritten, NULL))
        {
            CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo enviar el brillo por el puerto serie.");
        }
    }

    int read()
    {
        if (!ReadFile(_serialHandle, &_data, 1, &_bytesRead, NULL))
        {
            CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo leer del puerto serie.");
        }

        if (_bytesRead > 0)
        {
            if (_data != '\n')
            {
                _buffer += _data;
            }
            else
            {
                int value = std::stoi(_buffer);
                _buffer = "";
                return value;
            }
        }

        return -1;
    }

    static std::vector<std::string> buscar_puertos_serie()
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

private:
    HANDLE _serialHandle{nullptr};
    char _data{' '};
    DWORD _bytesRead{0};
    std::string _buffer{""};
};

void botonConectar_callback(Fl_Widget *w, void *data)
{
    if (puertoString == "")
    {
        // Sonido de error típico de Windows
        MessageBeep(MB_ICONHAND); // Otros: MB_OK, MB_ICONQUESTION, MB_ICONEXCLAMATION
        fl_message("Selecciona un puerto de la lista para conectar");
        return;
    }

    try
    {
        puertoSerie = make_unique<SerialPort>(puertoString); // construimos el objeto SerialPort a traves de su puntero inteligente, pasandole el string global
        Fl_Wizard *wizard = static_cast<Fl_Wizard *>(data);
        wizard->next();
        // CREAMOS EL TITULO DE LA SIGUIENTE VENTANA

    }
    catch (const std::runtime_error &e)
    {
        // fl_message("Error: %s", e.what()); // formato fprintf
        MessageBeep(MB_ICONEXCLAMATION);
        string mensaje = string("Error: ") + e.what();
        fl_message(mensaje.c_str());
    }
}

void desplegable_callback(Fl_Widget *w, void *data)
{
    Fl_Choice *pDesplegable = static_cast<Fl_Choice *>(w);
    Fl_Output *pTextoCOM = static_cast<Fl_Output *>(data); // pequeño texto para observar la variable global del puerto elegido

    int indice = pDesplegable->value();
    if (indice == -1) // nada seleccionado
    {
        return;
    }

    string puertoSeleccionado = pDesplegable->mvalue()->label(); // mvalue() devuelve el objeto menu item seleccionado. value() solo devuelve un entero del indice seleccionado
    pTextoCOM->value(puertoSeleccionado.c_str());
    puertoString = puertoSeleccionado;
}

void botonActualizar_callback(Fl_Widget *w, void *data)
{
    Fl_Choice *pDesplegable = static_cast<Fl_Choice *>(data);
    pDesplegable->clear();
    puertosGuardados = SerialPort::buscar_puertos_serie(); // puertosGuardados tiene que ser global porque el metodo add() recibe punteros de cada string en puertosGuardados. Al terminar la funcion callback, si puertosGuardados fuera local, se destruye la variable y los punteros apuntan a memoria rara (errores, comportamiento inesperado...)
    for (const string &p : puertosGuardados)
    {
        pDesplegable->add(p.c_str());
    }
    pDesplegable->value(-1);
    pDesplegable->redraw();
    puertoString = "";
}

class pantallaPrincipal : public Fl_Group
{

    Fl_Window *ventana;
    Fl_Wizard *wizard;

    Fl_Box *titulo;
    Fl_Button *botonVolver;
    Fl_Hor_Value_Slider *sliderPWM;

    static void botonVolver_callback(Fl_Widget *w, void *data){
        pantallaPrincipal *p = static_cast<pantallaPrincipal *>(data);
        puertoSerie.reset();  // destruye el SerialPort
        p->wizard->prev();
    }

     static void sliderPWM_callback(Fl_Widget *w, void *data){
        Fl_Hor_Value_Slider *pSlider = static_cast<Fl_Hor_Value_Slider *>(w);
        SerialPort *pPuerto = static_cast<SerialPort *>(data);
        int valorDelSlider = pSlider->value();
        pPuerto->send(valorDelSlider);
     }

public:
    pantallaPrincipal(Fl_Window *v, Fl_Wizard *w)
        : ventana(v),
          wizard(w),
          Fl_Group(0, 0, v->w(), v->h())
    {
        // TITULO
        const int xTitulo = 0;
        const int yTitulo = 0;
        const int wTitulo = ventana->w();
        const int hTitulo = 40;
        titulo = new Fl_Box{xTitulo, yTitulo, wTitulo, hTitulo, "Menú principal "}; // este label no se va a ver asi nunca, realmente tenemos que actualizar este label en el callback de botonConectar, cuando realmente ya hay un puertoString no vacio, no hay que olvidar que este codigo se ejecuta antes de llegar a fl_run y hayamos interactuado con la gui
        titulo->labelsize(24);

        // BOTON VOLVER (por ahora sirve para cambiar de puerto. Dara problemas en el futuro cuando haya mas cosas?)
        const int wBotonVolver = 80;
        const int hBotonVolver = 30;
        const int xBotonVolver = 0;
        const int yBotonVolver = 0;
        botonVolver = new Fl_Button{xBotonVolver, yBotonVolver, wBotonVolver, hBotonVolver, "Volver"};
        botonVolver->callback(botonVolver_callback, this);

        // SLIDER LAZO ABIERTO
        const int wSliderPWM = 200;
        const int hSliderPWM = 50;
        const int xSliderPWM = v->w()/2 - wSliderPWM/2;
        const int ySliderPWM = v->h()/2 - hSliderPWM/2;
        sliderPWM = new Fl_Hor_Value_Slider{xSliderPWM, ySliderPWM, wSliderPWM, hSliderPWM, "PWM"};
        sliderPWM->bounds(0, 255);
        sliderPWM->step(1);
        //sliderPWM->value(0);
        sliderPWM->type(FL_HOR_NICE_SLIDER);

        this->end(); // viene de Fl_Group.end()
    }
};

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
    botonActualizar_callback(&botonActualizar, &desplegableCOM); // llamada manual a la funcion de callback para que la aplicacion empiece con la lista cargada

    grupoBienvenida.end();

    /* =======================
       PANTALLA(GRUPO) PRINCIPAL
       ======================= */

    // Fl_Group grupoPrincipal(0, 0, ventana.w(), ventana.h());

    // // TITULO
    // Fl_Box titulo(0, 0, ventana.w(), 40, "Control de temperatura STM32");
    // titulo.labelsize(24);

    // grupoPrincipal.end();

    pantallaPrincipal pPrincipal{&ventana, &wizard};

    wizard.end();
    ventana.end();

    // wizard.value(&grupoPrincipal); // Página inicial

    ventana.show();
    return Fl::run();
}
