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

#include <cmath>

using namespace std;

// FORWARD DECLARATIONS (para poder usar tipos de datos antes de definirlos)
class SerialPort; // declaracion antes de definir la clase para poder definir el puntero inteligente de debajo
class pantallaPrincipal;

// VARIABLES GLOBALES
string puertoString = "";
vector<string> puertosGuardados;         // seria mejor que en vez de global, fuese una variable en la misma clase que el desplegable de puertos
std::unique_ptr<SerialPort> puertoSerie; // (NO ES UN PUNTERO, ES UN OBJETO QUE CONTIENE UN PUNTERO ENTRE OTRAS COSAS): puntero inteligente global para el objeto puerto serie, que se construye en el callback de boton conectar pero el objeto no vive en el ambito del callback (porque sino, se destruiria al finalizar el callback)

// DECLARACIONES DE METODOS DISPONIBLES PARA TODAS LAS PANTALLAS

/**
 * Convierte el valor analógico raw de la STM32 a grados Celsius.
 * @param adcValue Valor leído del ADC (0 - 4095 para 12 bits).
 * @return Temperatura en grados Celsius.
 */
double calcularCelsius(int adcValue);

// CLASES Y STRUCT

struct DataConectar
{
    Fl_Wizard *pWizard;
    pantallaPrincipal *pPrincipal;
};

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
        serialParams.BaudRate = CBR_115200;
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
        std::string data = std::to_string(value) + "\n"; // mandamos una cadena de caracteres (string, formato ASCII) terminada en \n (delimitador). Por ejemplo: "123\n"
        DWORD bytesWritten{0};                           // tipo de datos de windows (unsigned 32 bits). WriteFile escribre en esta variable cuantos bytes se han enviado

        if (!WriteFile(_serialHandle, data.c_str(), data.size(), &bytesWritten, NULL)) // aqui se produce el envio real. serialHandle representa al puerto abierto. NULL: operacion sincrona
        {
            CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo enviar el brillo por el puerto serie.");
        }
    }

    int read()
    {
        if (!ReadFile(_serialHandle, &_data, 1, &_bytesRead, NULL)) // aqui se produce la lectura de 1 byte.
        {
            CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo leer del puerto serie.");
        }

        if (_bytesRead > 0) // si se ha leido algo
        {
            if (_data != '\n')
            {
                _buffer += _data; // si lo leido no es el delimitador, lo guardas en el buffer
            }
            else // si es el delimitador, no lo guardas y:
            {
                int value = std::stoi(_buffer); //  conviertes el buffer de cadena de caracteres (ascii) a int
                _buffer = "";                   // limpieza del buffer para recibir el proximo dato
                return value;
            }
        }

        return -1; // cuando se este cargando el buffer (dato incompleto) o cuando no se haya leido nada aun
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

class pantallaPrincipal : public Fl_Group
{
    // Todas las funciones de callback deben ser static, para ser funciones de clase y no de objeto, y no llevar implicitamente el puntero this al objeto propio, ya que la firma que acepta FLTK debe ser la que es y no llevar nada extra
    static void botonVolver_callback(Fl_Widget *w, void *data)
    {
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data);
        puertoSerie.reset(); // destruye el SerialPort
        self->detener_lectura();    //  paramos el timer porque si no, sigue accediendo al timeout_callback y peta
        self->wizard->prev();
    }

    static void sliderPWM_callback(Fl_Widget *w, void *data)
    {
        Fl_Hor_Value_Slider *pSlider = static_cast<Fl_Hor_Value_Slider *>(w);

        unique_ptr<SerialPort> *ppInteligentePuerto = static_cast<unique_ptr<SerialPort> *>(data); // hay que hacer esto porque es puntero a puntero (puntero a objeto puntero inteligente)
        SerialPort *pPuerto = ppInteligentePuerto->get();

        int valorDelSlider = pSlider->value();

        if (!ppInteligentePuerto || !ppInteligentePuerto->get()) // por seguridad aunque creo que no hace falta
        {
            fl_message("Puerto no conectado");
            return;
        }
        pPuerto->send(valorDelSlider);
    }

    static void timeout_callback(void *data)
    {
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data); // es util usar la nomenclatura self cuando tienes un puntero que hace referencia a la misma clase en la que estas (como en python)

        int lectura = puertoSerie->read(); // acceso a la variable global a traves del puntero inteligente (tiene un operador "->" que hace que se pueda acceder a él como si fuera un puntero)
        if(lectura >= 0){  //esperar al dato completo para mostrarlo
            //double tempCelsius = calcularCelsius(lectura);
            self->textoTemperaturaDigital->value(to_string(lectura).c_str());
        }
        Fl::repeat_timeout(0.01, timeout_callback, data); // REPROGRAMAR TIMER
    }

public:
    // uso atributos puntero para poder crear los objetos en el cuerpo del constructor (como me gusta mas a mi para tener encima las const int de dimensionado y tener todo junto)
    // en consecuencia, tengo que usar "new", pero no tengo que preocuparme de "delete" porque FLTK gestiona automaticamente la destruccion de los hijos
    Fl_Window *ventana;
    Fl_Wizard *wizard;

    Fl_Box *titulo;
    Fl_Button *botonVolver;
    Fl_Hor_Value_Slider *sliderPWM;
    Fl_Output *textoTemperaturaDigital;

    void activar_lectura()
    { // funcion que no es estatica porque requiere de que haya un objeto instanciado (y ademas no requiere una firma concreta impuesta)
        Fl::add_timeout(0.01, timeout_callback, this);
    }

    void detener_lectura()
    {
        Fl::remove_timeout(timeout_callback, this);
    }

    pantallaPrincipal(Fl_Window *v, Fl_Wizard *w)
        : ventana(v),
          wizard(w),
          Fl_Group(0, 0, v->w(), v->h())
    {
        // TITULO
        const int xTitulo = 0;
        const int yTitulo = 10;
        const int wTitulo = ventana->w();
        const int hTitulo = 40;
        titulo = new Fl_Box{xTitulo, yTitulo, wTitulo, hTitulo, "PANEL DE CONTROL"};
        titulo->labelsize(30);

        // BOTON VOLVER (por ahora sirve para cambiar de puerto. Dara problemas en el futuro cuando haya mas cosas?)
        const int wBotonVolver = 100;
        const int hBotonVolver = 40;
        const int xBotonVolver = 0 + 10;
        const int yBotonVolver = 0 + 10;
        botonVolver = new Fl_Button{xBotonVolver, yBotonVolver, wBotonVolver, hBotonVolver, "Volver"};
        botonVolver->labelsize(20);
        botonVolver->callback(botonVolver_callback, this);

        // SLIDER LAZO ABIERTO
        const int wSliderPWM = 200;
        const int hSliderPWM = 30;
        const int xSliderPWM = v->w() / 2 - wSliderPWM / 2;
        const int ySliderPWM = v->h() / 2 - hSliderPWM / 2;
        sliderPWM = new Fl_Hor_Value_Slider{xSliderPWM, ySliderPWM, wSliderPWM, hSliderPWM, "PWM"};
        sliderPWM->callback(sliderPWM_callback, &puertoSerie);
        sliderPWM->labelsize(18);
        sliderPWM->textsize(16);
        sliderPWM->bounds(0, 255);
        sliderPWM->step(1);
        sliderPWM->type(FL_HOR_NICE_SLIDER);

        // TEXTO DE TEMPERATURA DIGITAL LEIDA
        const int wTextoTemperaturaDigital = 70;
        const int hTextoTemperaturaDigital = 50;
        const int xTextoTemperaturaDigital = v->w()/2 - wTextoTemperaturaDigital/2;
        const int yTextoTemperaturaDigital = v->h() - hTextoTemperaturaDigital*(5/4.0);
        textoTemperaturaDigital = new Fl_Output{xTextoTemperaturaDigital, yTextoTemperaturaDigital, wTextoTemperaturaDigital, hTextoTemperaturaDigital, "Temperatura digital"};
        textoTemperaturaDigital->labelsize(18);
        textoTemperaturaDigital->textsize(16);
        this->end(); // viene de Fl_Group.end()
    }
};

// Declaraciones de metodos de pantalla de bienvenida para poder definirlos despues de ambas pantallas y solucionar las dependencias circulares
void botonConectar_callback(Fl_Widget *w, void *data);
void desplegable_callback(Fl_Widget *w, void *data);
void botonActualizar_callback(Fl_Widget *w, void *data);


int main()
{

    Fl_Window ventana(0, 0, 1200, 676, "Control de temperatura - Alberto Serrano Moreno");
    Fl_Wizard wizard{0, 0, ventana.w(), ventana.h()};   // widget invisible con el mismo tamaño que la ventana que nos sirve para iterar la visibilidad de sus grupos hijos

    pantallaPrincipal *pPrincipal = nullptr; //  puntero vacio por ahora. Para poder pasar la direccion de principal antes de que el objeto haya sido creado

    /* =======================
       PANTALLA(GRUPO) BIENVENIDA
       ======================= */
    Fl_Group grupoBienvenida(0, 0, ventana.w(), ventana.h());

    // TITULO
    Fl_Box tituloBienvenida(0, ventana.h() / 4, ventana.w(), 40, "Control PID de temperatura");
    tituloBienvenida.labelsize(40);

    // BOTON CONECTAR
    const int wBotonConectar = 300;
    const int hBotonConectar = 60;
    const int xBotonConectar = ventana.w() / 2 - wBotonConectar / 2;
    const int yBotonConectar = ventana.h() / 2;
    Fl_Button botonConectar{xBotonConectar, yBotonConectar, wBotonConectar, hBotonConectar, "Conectar"};
    botonConectar.labelsize(24);
    DataConectar dataConectar{&wizard, pPrincipal};
    botonConectar.callback(botonConectar_callback, &dataConectar);

    // DESPLEGABLE PUERTOS SERIE
    const int wDesplegable = wBotonConectar / 2;
    const int hDesplegable = 40;
    const int xDesplegable = xBotonConectar;
    const int yDesplegable = yBotonConectar + (hBotonConectar / 2) + hDesplegable;
    Fl_Choice desplegableCOM{xDesplegable, yDesplegable, wDesplegable, hDesplegable, "Puerto serie:"};
    desplegableCOM.labelsize(20);
    desplegableCOM.textsize(20);
    desplegableCOM.callback(desplegable_callback);

    // BOTON ACTUALIZAR PUERTOS SERIE DEL DESPLEGABLE
    const int wBotonActualizar = wBotonConectar / 2;
    const int hBotonActualizar = hDesplegable;
    const int xBotonActualizar = xDesplegable + wDesplegable;
    const int yBotonActualizar = yDesplegable;
    Fl_Button botonActualizar{xBotonActualizar, yBotonActualizar, wBotonActualizar, hBotonActualizar, "Actualizar"};
    botonActualizar.labelsize(20);
    botonActualizar.callback(botonActualizar_callback, &desplegableCOM);
    botonActualizar_callback(&botonActualizar, &desplegableCOM); // llamada manual a la funcion de callback para que la aplicacion empiece con la lista cargada

    grupoBienvenida.end();

    /* =======================
       PANTALLA(GRUPO) PRINCIPAL
       ======================= */

    pantallaPrincipal principal{&ventana, &wizard};
    dataConectar.pPrincipal = &principal; // una vez creada principal, pasamos su direccion a su puntero para poder usarse en el struct de conectar

    wizard.end();
    ventana.end();

    // wizard.value(&grupoPrincipal); // Página inicial

    ventana.show();

    return Fl::run();
}

// LOGICA (Funciones que solo hayan sido declaradas como prototipos y aun no definidas)

void botonConectar_callback(Fl_Widget *w, void *data)
{
    if (puertoString == "")
    {
        // sonido de error tipico de windows
        MessageBeep(MB_ICONHAND); // Otros: MB_OK, MB_ICONQUESTION, MB_ICONEXCLAMATION
        fl_message("Selecciona un puerto de la lista para conectar");
        return;
    }

    try
    {
        puertoSerie = make_unique<SerialPort>(puertoString); // construimos el objeto SerialPort a traves de su puntero inteligente, pasandole el string global
        DataConectar *dataConectar = static_cast<DataConectar *>(data);
        // Fl_Wizard *pWizard = dataConectar->pWizard;
        // pantallaPrincipal *pPantallaPrincipal = dataConectar->pPrincipal;
        dataConectar->pWizard->next();
        // TIMER PERIODICO
        dataConectar->pPrincipal->activar_lectura();
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
    //Fl_Output *pTextoCOM = static_cast<Fl_Output *>(data); // pequeño texto para observar la variable global del puerto elegido

    int indice = pDesplegable->value();
    if (indice == -1) // nada seleccionado
    {
        return;
    }

    string puertoSeleccionado = pDesplegable->mvalue()->label(); // mvalue() devuelve el objeto menu item seleccionado. value() solo devuelve un entero del indice seleccionado
    //pTextoCOM->value(puertoSeleccionado.c_str());
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

double calcularCelsius(int adcValue) {
    // 1. Configuración del ADC (10 bits para STM32 en framework Arduino)
    const double ADC_MAX = 1023.0; 
    
    // 2. Parámetros del hardware (Resistencia R3 en el esquemático)
    const double RESISTENCIA_FIJA = 10000.0; // 10k Ohms
    
    // 3. Parámetros del NTC (según datasheet)
    const double R0 = 10000.0;    // Resistencia a 25°C
    const double T0 = 298.15;     // 25°C en Kelvin (273.15 + 25)
    const double BETA = 3380.0;   // Constante B (25/50°C)

    // Validación de seguridad para evitar divisiones por cero o logaritmos negativos
    if (adcValue <= 0 || adcValue >= (int)ADC_MAX) {
        return 0.0;
    }

    // --- CÁLCULO ---

    // Paso A: Calcular la resistencia actual del termistor
    // Según tu esquema: VCC -> NTC -> ADC -> R3 -> GND
    double rNtc = RESISTENCIA_FIJA * (ADC_MAX / (double)adcValue - 1.0);

    // Paso B: Aplicar la ecuación Beta (Variante de Steinhart-Hart)
    double logR = std::log(rNtc / R0);
    double temperaturaK = 1.0 / (1.0 / T0 + logR / BETA);

    // Paso C: Convertir de Kelvin a Celsius
    return temperaturaK - 273.15;
}