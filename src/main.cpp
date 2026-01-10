#include <FL/Fl.h>
#include <FL/Fl_Window.h>
#include <FL/Fl_Button.h>
#include <FL/Fl_Box.h>
#include <FL/Fl_Wizard.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Output.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Hor_Value_Slider.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Pack.H>

#include <windows.h>
#include <vector>
#include <string>

#include <stdexcept>

#include <memory>

#include <windows.h>

#include <cmath>

#include <iostream>

#include <FL/Fl_Widget.H>
#include <FL/fl_draw.H>

using namespace std;

#define PERIODO_INTERRUPCION_PERIODICA 0.05 // en segundos. Actualmente 10ms. Deberia poder subirse sin problema hasta 50ms y los datos siguen llegando a la misma velocidad (cada 100ms)

#define KP0 8.0
#define KI0 0.6
#define KD0 2.0
#define REF0 600

void abrirConsolaDebug()
{
#ifdef _WIN32
    AllocConsole();
    FILE *stream;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    std::cout.clear();
    std::cerr.clear();
#endif
}

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

struct DataConectar // datos para el callback de boton conectar en la pantalla de bienvenida
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
        // BORRAR LOS BUFFERS INTERNOS DE WINDOWS PARA EL PUERTO:
        // PURGE_RXCLEAR: Borra el buffer de entrada (lectura)
        // PURGE_TXCLEAR: Borra el buffer de salida (escritura)
        PurgeComm(_serialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
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
            throw std::runtime_error("No se pudo enviar el dato por el puerto serie.");
        }
    }

    void sendString(const std::string &data)
    {
        DWORD bytesWritten{0};
        if (!WriteFile(_serialHandle, data.c_str(), data.size(), &bytesWritten, NULL))
        {
            CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo enviar la cadena por el puerto serie.");
        }

        // DEBUG: imprimir en consola lo que se envía
        std::cout << "[SERIAL OUT] ";
        for (char c : data)
        {
            if (c == '\n')
                std::cout << "\\n"; // marcar salto de línea
            else
                std::cout << c;
        }
        std::cout << std::endl;
        // ----
    }

    // Esta funcion NO lee byte a byte, si no en bloques de hasta 64 bytes (y funciona igual, leyendo hasta el delimitador \n)
    // esto permite leer los mensajes que mando desde el STM (DATA ... (30 bytes aprox)) en UNA SOLA ITERACION
    // de este modo, si el timeout_callback se ejecuta cada 10ms, cada 10ms podemos leer un mensaje completo de 64 bytes, en vez de 1 solo byte como antes
    // Si leyera byte a byte como antes, los mensajes que recibo del STM que ocupan unos 30 bytes, tardaria en leerlos 30 iteraciones de readString
    // teniendo el timeout cada 10 ms, eso son 300ms en leer UN SOLO MENSAJE
    // dado que en el arduino ejecutamos el lazo de control y enviamos mensaje cada 100 ms, estariamos leyendo 3 veces mas lento de lo que se generan los mensajes
    // eso generaria un lag tremendo, con datos que no se corresponden con el momento actual (comprobado) y un desborde del buffer interno de windows que nunca se vacía
    string readString()
    {
        char tempBuffer[64];                                                              // buffer temporal de lectura
        if (!ReadFile(_serialHandle, &tempBuffer, sizeof(tempBuffer), &_bytesRead, NULL)) // aqui se produce la lectura de sizeof(tempBuffer) = 64 bytes
        {
            CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo leer del puerto serie.");
        }

        if (_bytesRead == 0)
            return ""; // si no llega nada, devolvemos string vacio

        // Si ha llegado algo (un bloque, de 64 bytes maximo):
        for (int i = 0; i < _bytesRead; ++i) // leemos byte a byte el bloque
        {
            char byteLeido = tempBuffer[i];
            if (byteLeido != '\n') // si no es el delimitador, guardamos el byte (es contenido)
            {
                _buffer += byteLeido;
            }
            else // es delimitador, no lo guardamos (el '\n') y terminamos el mensaje porque ya esta completo
            {
                string mensaje = _buffer;
                _buffer = "";
                return mensaje;
            }
        }
        // si se llega hasta aqui, significa que el for ha terminado de leer el bloque entero pero no ha encontrado ningun \n
        // eso significa que se esta mandando un mensaje muy largo, de mas de 64 bytes
        // en tal caso se necesita mas de una iteracion de readString para leerlo completamente
        // mientras tanto, hay que devolver algo porque la funcion debe devolver un string siempre
        // (no aplica en mi caso porque no voy a mandar mensajes tan largos desde el STM, pero bueno)
        return "";
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
                int value = -1; // Valor por defecto de error
                try
                {
                    if (!_buffer.empty())
                    {
                        value = std::stoi(_buffer);
                    }
                }
                catch (...) // captura cualquier tipo de error
                {
                    // si falla stoi() (llega ruido o algo que no sea un numero) no hacemos nada, simplemente devolvemos -1 y el programa sigue vivo
                    // Opcional: imprimir error en consola debug
                    std::cerr << "Error de trama recibida: " << _buffer << std::endl;
                    value = -1;
                }
                _buffer = "";
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

class SerialData
{
public:
    double kp;
    double ki;
    double kd;
    double consigna;

    enum class Modo
    {
        LAZO_ABIERTO,
        LAZO_CERRADO
    };

    struct DatosGraficos
    {
        float consigna = 0.0f;
        float temperatura = 0.0f;
        float error = 0.0f;
        int pwm = 0;
    };

    Modo modo = Modo::LAZO_ABIERTO;
    DatosGraficos datosGraficos;

    SerialData(double p = 1.0, double i = 1.0, double d = 1.0, int c = 0.0) : kp(p), ki(i), kd(d), consigna(c)
    {
    }

    void actualizarDatosPID(double kp, double ki, double kd, double consigna)
    {
        this->kp = kp;
        this->ki = ki;
        this->kd = kd;
        this->consigna = consigna;
    }

    void actualizarModo(Modo m)
    {
        this->modo = m;
    }

    void enviarMensajePID()
    {
        char buffer[64];                      // creamos un array de caracteres con tamaño suficiente para el mensaje
        snprintf(buffer, sizeof(buffer),      // snprintf es como printf pero no escribe en consola, si no en un char[]. De esta manera controlamos perfectamente el tamaño y formato el mensaje que se enviara
                 "PID %.2f %.2f %.2f %.2f\n", // PID <kp.00> <ki.00> <kd.00> <consigna.00>
                 kp, ki, kd, consigna);
        puertoSerie->sendString(buffer);
    }

    void enviarMensajeMODO()
    {
        int modoInt;
        if (modo == Modo::LAZO_ABIERTO)
        {
            modoInt = 0;
        }
        else
        {
            modoInt = 1;
        }

        char buffer[16];
        snprintf(buffer, sizeof(buffer),
                 "MODO %d\n", // MODO <0 = lazo abierto || 1 = lazo cerrado>
                 modoInt);
        puertoSerie->sendString(buffer);
    }

    void enviarMensajePWM(int pwm)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer),
                 "PWM %d\n", // PWM <valor>
                 pwm);
        puertoSerie->sendString(buffer);
    }

    void leerMensajeDatosGraficos(string mensaje)
    {
        if (mensaje.find("DATA") == 0)
        {
            int tokensRecogidos = 0;
            float c, t, e;
            int p;
            tokensRecogidos = sscanf(mensaje.c_str(),
                                     "DATA %f %f %f %d", // DATA <CONSIGNA.00> <TEMPERATURA.00> <ERROR.00> <PWM>
                                     &c, &t, &e, &p);
            // nos aseguramos de que se ha leido correctamente antes de cambiar nada
            if (tokensRecogidos == 4)
            {
                datosGraficos.consigna = c;
                datosGraficos.temperatura = t;
                datosGraficos.error = e;
                datosGraficos.pwm = p;
            }
        }
    }
};

class Grafica : public Fl_Widget
{
private:
    struct Serie
    {
        std::vector<float> buffer;
        Fl_Color color;
        float minValor;
        float maxValor;

        Serie(Fl_Color c, int capacidad, float minV, float maxV)
            : color(c), minValor(minV), maxValor(maxV)
        {
            buffer.reserve(capacidad);
        }
    };

    std::vector<Serie> series;
    size_t maxPuntos;

    // 🔹 OFFSCREEN BUFFER
    Fl_Offscreen offscreen = 0;

    static constexpr int MARGEN_Y = 8;

public:
    Grafica(int x, int y, int w, int h, const char *label = nullptr)
        : Fl_Widget(x, y, w, h, label), maxPuntos(w)
    {
        box(FL_FLAT_BOX);

        offscreen = fl_create_offscreen(w, h);
    }

    ~Grafica()
    {
        if (offscreen)
            fl_delete_offscreen(offscreen);
    }

    int añadirSerie(Fl_Color color, float minV, float maxV)
    {
        series.emplace_back(color, maxPuntos, minV, maxV);
        return series.size() - 1;
    }

    void añadirDato(int idSerie, float valor)
    {
        if (idSerie < 0 || idSerie >= (int)series.size())
            return;

        Serie &s = series[idSerie];

        if (s.buffer.size() >= maxPuntos)
            s.buffer.erase(s.buffer.begin());

        s.buffer.push_back(valor);

        redraw();
    }

    void resize(int X, int Y, int W, int H) override
    {
        Fl_Widget::resize(X, Y, W, H);

        maxPuntos = W;

        if (offscreen)
            fl_delete_offscreen(offscreen);

        offscreen = fl_create_offscreen(W, H);
    }

    void draw() override
    {
        if (!offscreen)
            return;

        // ==========================
        // DIBUJO EN MEMORIA
        // ==========================
        fl_begin_offscreen(offscreen);

        fl_push_clip(0, 0, w(), h());

        // Fondo
        fl_color(FL_WHITE);
        fl_rectf(0, 0, w(), h());

        // Rejilla simple (línea central)
        fl_color(fl_rgb_color(200, 200, 200));
        int yMedio = h() / 2;
        fl_line(0, yMedio, w(), yMedio);

        // Series
        for (const auto &s : series)
        {
            if (s.buffer.size() < 2)
                continue;

            fl_color(s.color);
            fl_line_style(FL_SOLID, 2);

            float rango = s.maxValor - s.minValor;
            if (rango == 0)
                rango = 1;

            for (size_t i = 1; i < s.buffer.size(); ++i)
            {
                float v0 = s.buffer[i - 1];
                float v1 = s.buffer[i];

                float n0 = (v0 - s.minValor) / rango;
                float n1 = (v1 - s.minValor) / rango;

                int y0 = (h() - MARGEN_Y) - n0 * (h() - 2 * MARGEN_Y);
                int y1 = (h() - MARGEN_Y) - n1 * (h() - 2 * MARGEN_Y);

                int x0 = i - 1;
                int x1 = i;

                fl_line(x0, y0, x1, y1);
            }
        }

        fl_line_style(0);

        // Marco
        fl_color(FL_BLACK);
        fl_rect(0, 0, w(), h());

        fl_pop_clip();

        fl_end_offscreen();

        // ==========================
        // COPIA ATÓMICA A PANTALLA
        // ==========================
        fl_copy_offscreen(x(), y(), w(), h(), offscreen, 0, 0);
    }
};


class pantallaPrincipal : public Fl_Group
{
private:
    // Todas las funciones de callback deben ser static, para ser funciones de clase y no de objeto, y no llevar implicitamente el puntero this al objeto propio, ya que la firma que acepta FLTK debe ser la que es y no llevar nada extra
    static void botonVolver_callback(Fl_Widget *w, void *data)
    {
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data);
        puertoSerie.reset();     // destruye el SerialPort
        self->detener_lectura(); //  paramos el timer porque si no, sigue accediendo al timeout_callback y peta
        self->wizard->prev();
    }

    static void sliderPWM_callback(Fl_Widget *w, void *data)
    {
        Fl_Hor_Value_Slider *pSlider = static_cast<Fl_Hor_Value_Slider *>(w);
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data);

        int valorDelSlider = pSlider->value();
        self->serialData.enviarMensajePWM(valorDelSlider);
    }

    static void timeout_callback(void *data) // lectura periodica del puerto serie
    {
        // si el puerto no existe (cerrado), no hacemos nada y NO reprogramamos el timer
        if (!puertoSerie)
            return;

        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data); // es util usar la nomenclatura self cuando tienes un puntero que hace referencia a la misma clase en la que estas (como en python)

        string mensaje = puertoSerie->readString(); // acceso a la variable global a traves del puntero inteligente (tiene un operador "->" que hace que se pueda acceder a él como si fuera un puntero)

        if (mensaje != "")
        { // esperar al dato completo (no es necesario con la nueva funcion readString que lee el mensaje de una vez en lugar de byte a byte)
            self->serialData.leerMensajeDatosGraficos(mensaje);

            float temperatura_recibida = self->serialData.datosGraficos.temperatura;
            float consigna_recibida = self->serialData.datosGraficos.consigna;
            float error_recibido = self->serialData.datosGraficos.error;
            int pwm_recibido = self->serialData.datosGraficos.pwm;

            self->textoADC->value(static_cast<double>(temperatura_recibida));
            self->textoConsigna->value(consigna_recibida);
            self->textoError->value(error_recibido);
            self->textoPWM->value(pwm_recibido);

            self->graficas->añadirDato(self->idADC, temperatura_recibida);
            self->graficas->añadirDato(self->idConsigna, consigna_recibida);
            self->graficas->añadirDato(self->idError, error_recibido);
            self->graficas->añadirDato(self->idPWM, pwm_recibido);
        }
        Fl::repeat_timeout(PERIODO_INTERRUPCION_PERIODICA, timeout_callback, data); // REPROGRAMAR TIMER
    }

    void config_GUI_lazo_cerrado()
    {
        sliderPWM->deactivate();
        inputKp->activate();
        inputKi->activate();
        inputKd->activate();
        botonActualizarPID->activate();
        sliderREF->activate();

        textoConsigna->activate();
        textoError->activate();
    }

    void config_GUI_lazo_abierto()
    {
        rbLazoAbierto->value(1); // por defecto comienza encendido. Por tanto, el otro radio button comienza apagado (por pertenecer ambos al mismo grupo)
        sliderPWM->activate();
        inputKp->deactivate();
        inputKi->deactivate();
        inputKd->deactivate();
        botonActualizarPID->deactivate();
        sliderREF->deactivate();

        textoConsigna->deactivate();
        textoError->deactivate();
    }

    static void radio_callback(Fl_Widget *w, void *data) // accion de los radio buttons, para conmutar entre lazo abierto y cerrado
    {
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data);
        Fl_Round_Button *rb = static_cast<Fl_Round_Button *>(w);
        if (self->rbLazoCerrado->value() == 1)
        {
            // LAZO CERRADO
            self->config_GUI_lazo_cerrado();
            self->serialData.actualizarModo(SerialData::Modo::LAZO_CERRADO);
            self->serialData.enviarMensajeMODO();
            self->actualizarPID_callback(self->botonActualizarPID, self); // para mandar los valores visibles mantalla nada mas cambiar a lazo cerrado, sin tener que pulsar el boton la primera vez
        }
        else
        {
            // LAZO ABIERTO
            self->config_GUI_lazo_abierto();
            self->serialData.actualizarModo(SerialData::Modo::LAZO_ABIERTO);
            self->serialData.enviarMensajeMODO();
            self->sliderPWM_callback(self->sliderPWM, self); // para mandar el valor visible en el slider inmediatamente, sin tener que moverlo la primera vez
        }
    }

    void configurarPanelControl(const int wPanel, const int hPanel, const int xPanel, const int yPanel, const int margenPanel); // prototipo

    static void actualizarPID_callback(Fl_Widget *w, void *data)
    {
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data);

        self->serialData.actualizarDatosPID(self->inputKp->value(), self->inputKi->value(), self->inputKd->value(), self->sliderREF->value());
        self->serialData.enviarMensajePID(); // manda por el puerto serie el PID y la consigna
    }

public:
    // uso atributos puntero para poder crear los objetos en el cuerpo del constructor (como me gusta mas a mi para tener encima las const int de dimensionado y tener todo junto)
    // en consecuencia, tengo que usar "new", pero no tengo que preocuparme de "delete" porque FLTK gestiona automaticamente la destruccion de los hijos
    Fl_Window *ventana;
    Fl_Wizard *wizard;

    Fl_Box *titulo;
    Fl_Button *botonVolver;

    Fl_Group *panelControl;
    Fl_Box *tituloPanel;
    Fl_Round_Button *rbLazoCerrado;
    Fl_Round_Button *rbLazoAbierto;
    Fl_Hor_Value_Slider *sliderPWM;
    Fl_Value_Input *inputKp;
    Fl_Value_Input *inputKi;
    Fl_Value_Input *inputKd;
    Fl_Hor_Value_Slider *sliderREF;
    Fl_Button *botonActualizarPID;

    SerialData serialData;

    Fl_Group *grupoColumnaDatosGraficos;
    Fl_Pack *columnaDatosGraficos;
    Fl_Output *textoADC;
    Fl_Output *textoConsigna;
    Fl_Output *textoError;
    Fl_Output *textoPWM;

    Grafica *graficas;
    int idADC, idConsigna, idError, idPWM;

    void activar_lectura()
    { // funcion que no es estatica porque requiere de que haya un objeto instanciado (y ademas no requiere una firma concreta impuesta)
        Fl::add_timeout(PERIODO_INTERRUPCION_PERIODICA, timeout_callback, this);
    }

    void detener_lectura()
    {
        Fl::remove_timeout(timeout_callback, this);
    }

    void actualizar_titulo()
    {
        string nuevoTitulo = "MENÚ PRINCIPAL || Conectado a " + puertoString;
        this->titulo->copy_label(nuevoTitulo.c_str());
    }

    void setup(); // prototipo

    // CONSTRUCTOR
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
        titulo = new Fl_Box{xTitulo, yTitulo, wTitulo, hTitulo, "MENÚ PRINCIPAL"};
        titulo->labelsize(30);
        titulo->labelfont(FL_BOLD);

        // BOTON VOLVER (por ahora sirve para cambiar de puerto. Dara problemas en el futuro cuando haya mas cosas?)
        // se me ocurre que quiza sea mejor que sea un boton "reiniciar", que te lleve a la pantalla de bienvenida pero reseteando todo, como si volvieras a ejecutar el programa
        const int wBotonVolver = 100;
        const int hBotonVolver = 40;
        const int xBotonVolver = 15;
        const int yBotonVolver = 15;
        botonVolver = new Fl_Button{xBotonVolver, yBotonVolver, wBotonVolver, hBotonVolver, "Volver"};
        botonVolver->labelsize(20);
        botonVolver->labelfont(FL_BOLD);
        botonVolver->callback(botonVolver_callback, this);

        configurarPanelControl(300,
                               v->h() - 2 * (yBotonVolver + hBotonVolver + 30),
                               xBotonVolver,
                               yBotonVolver + hBotonVolver + 30,
                               10);

        // COLUMNA DE VALORES DE LAS VARIABLES GRAFICADAS
        // esto ira luego en el panel de graficas
        constexpr int hWidgetsColumnaDatosGraficos = 35;
        constexpr int spacingWidgetsColumnaDatosGraficos = 80;
        constexpr int margen = 15;
        // funcion lambda
        auto formatoWidgetsColumnaDatosGraficos = [](Fl_Output *w)
        {
            w->labelsize(18);
            w->textsize(16);
            w->align(FL_ALIGN_BOTTOM);
        };
        grupoColumnaDatosGraficos = new Fl_Group{panelControl->x() + panelControl->w() + 50,
                                                 panelControl->y(),
                                                 100 + 2 * margen,
                                                 //  4 * hWidgetsColumnaDatosGraficos + 4 * spacingWidgetsColumnaDatosGraficos + 1 * margen};
                                                 panelControl->h()};
        grupoColumnaDatosGraficos->box(FL_THIN_UP_BOX);
        columnaDatosGraficos = new Fl_Pack(grupoColumnaDatosGraficos->x() + margen,
                                           grupoColumnaDatosGraficos->y() + (grupoColumnaDatosGraficos->h()) / 2 - (4 * hWidgetsColumnaDatosGraficos + 3 * spacingWidgetsColumnaDatosGraficos) / 2,
                                           100,
                                           0);
        columnaDatosGraficos->type(Fl_Pack::VERTICAL);
        columnaDatosGraficos->spacing(spacingWidgetsColumnaDatosGraficos); // espacio vertical entre widgets

        // TEXTO DE TEMPERATURA DIGITAL LEIDA (ADC)
        textoADC = new Fl_Output{0, 0, 0, hWidgetsColumnaDatosGraficos, "ADC"};
        formatoWidgetsColumnaDatosGraficos(textoADC);

        // TEXTO CONSIGNA
        textoConsigna = new Fl_Output{0, 0, 0, hWidgetsColumnaDatosGraficos, "Consigna"};
        formatoWidgetsColumnaDatosGraficos(textoConsigna);

        // TEXTO ERROR
        textoError = new Fl_Output{0, 0, 0, hWidgetsColumnaDatosGraficos, "Error"};
        formatoWidgetsColumnaDatosGraficos(textoError);

        // TEXTO PWM
        textoPWM = new Fl_Output{0, 0, 0, hWidgetsColumnaDatosGraficos, "PWM"};
        formatoWidgetsColumnaDatosGraficos(textoPWM);

        columnaDatosGraficos->end();
        grupoColumnaDatosGraficos->end();

        graficas = new Grafica{grupoColumnaDatosGraficos->x() + grupoColumnaDatosGraficos->w() + 0,
                               grupoColumnaDatosGraficos->y(),
                               650,
                               grupoColumnaDatosGraficos->h(),
                               "GRÁFICAS"};
        //graficas->setRango(-500.0, 1000.0);
        idADC = graficas->añadirSerie(FL_GREEN, 500.0, 1000.0);
        idConsigna = graficas->añadirSerie(FL_BLUE, 500.0, 1000.0);
        idError = graficas->añadirSerie(FL_RED, -500.0, 500.0);
        idPWM = graficas->añadirSerie(FL_MAGENTA, 0.0, 255.0);

        config_GUI_lazo_abierto(); // Comenzamos en lazo abierto por defecto
        this->end();               // viene de Fl_Group.end()
    }
};

// DEFINICIONES DE PROTOTIPOS DE CLASE
void pantallaPrincipal::configurarPanelControl(const int wPanel, const int hPanel, const int xPanel, const int yPanel, const int margenPanel)
{
    // --- PANEL DE CONTROL ---
    panelControl = new Fl_Group{xPanel, yPanel, wPanel, hPanel};
    panelControl->box(FL_THIN_UP_BOX);

    // Todo lo del panel debe tener el mismo ancho y margen. Tambien misma x. La y es lo que se va incrementando.
    const int xElementosPanel = xPanel + margenPanel;
    int yElementosPanel = yPanel + margenPanel;
    const int wElementosPanel = wPanel - 2 * margenPanel;

    // Titulo del panel de control
    tituloPanel = new Fl_Box(xElementosPanel,
                             yElementosPanel,
                             wElementosPanel,
                             30,
                             "Panel de control");
    tituloPanel->labelsize(20);
    tituloPanel->labelfont(FL_BOLD);
    tituloPanel->align(FL_ALIGN_CENTER);

    // SELECTORES DE LAZO ABIERTO/CERRADO (POR DEFECTO EMPIEZA EN LAZO ABIERTO)
    yElementosPanel += 50;
    rbLazoAbierto = new Fl_Round_Button{xElementosPanel,
                                        yElementosPanel,
                                        wElementosPanel,
                                        30,
                                        "Control manual (lazo abierto)"};
    rbLazoAbierto->type(FL_RADIO_BUTTON);
    rbLazoAbierto->callback(radio_callback, this);

    yElementosPanel += 25;
    rbLazoCerrado = new Fl_Round_Button{xElementosPanel,
                                        yElementosPanel,
                                        wElementosPanel,
                                        30,
                                        "Control automático (lazo cerrado)"};
    rbLazoCerrado->type(FL_RADIO_BUTTON);
    rbLazoCerrado->callback(radio_callback, this);

    // SLIDER PWM LAZO ABIERTO
    yElementosPanel += 50;
    sliderPWM = new Fl_Hor_Value_Slider{xElementosPanel,
                                        yElementosPanel,
                                        wElementosPanel,
                                        30,
                                        "PWM"};
    sliderPWM->callback(sliderPWM_callback, this);
    sliderPWM->labelsize(18);
    sliderPWM->textsize(16);
    sliderPWM->bounds(0, 255);
    sliderPWM->step(1);
    sliderPWM->type(FL_HOR_NICE_SLIDER);

    // PARAMETROS PID LAZO CERRADO
    yElementosPanel += 70;
    inputKp = new Fl_Value_Input{xElementosPanel + 25,
                                 yElementosPanel,
                                 wElementosPanel - 50,
                                 30,
                                 "Kp:"};
    yElementosPanel += 50;
    inputKi = new Fl_Value_Input{xElementosPanel + 25,
                                 yElementosPanel,
                                 wElementosPanel - 50,
                                 30,
                                 "Ki:"};
    yElementosPanel += 50;
    inputKd = new Fl_Value_Input{xElementosPanel + 25,
                                 yElementosPanel,
                                 wElementosPanel - 50,
                                 30,
                                 "Kd:"};

    inputKp->value(KP0);
    inputKp->step(0.1);

    inputKi->value(KI0);
    inputKi->step(0.01);

    inputKd->value(KD0);
    inputKd->step(0.1);

    yElementosPanel += 50;
    sliderREF = new Fl_Hor_Value_Slider{xElementosPanel,
                                        yElementosPanel,
                                        wElementosPanel,
                                        30,
                                        "Consigna (digital)"};
    sliderREF->labelsize(18);
    sliderREF->textsize(16);
    sliderREF->bounds(500, 1000);
    sliderREF->step(1);
    sliderREF->value(REF0);
    sliderREF->type(FL_HOR_NICE_SLIDER);

    // BOTON ACTUALIZAR PID
    yElementosPanel += 70;
    botonActualizarPID = new Fl_Button{xElementosPanel,
                                       yElementosPanel,
                                       wElementosPanel,
                                       30,
                                       "Actualizar PID"};
    botonActualizarPID->callback(actualizarPID_callback, this);

    panelControl->end();
}

void pantallaPrincipal::setup()
{
    // Inicializamos cosas que no puedan/deban inicializarse en el constructor de pPrincipal.
    // esta funcion se llamara desde el callback del boton conectar de la pantalla de bienvenida, cuando el puerto serie ya esta abierto y la pantalla principal deja de ser invisible y ha de ser usable
    // seran cosas independientes de la interfaz (es bueno separar la logica de la interfaz y la logica de comportamiento)
    activar_lectura();
    actualizar_titulo();
    serialData.actualizarModo(SerialData::Modo::LAZO_ABIERTO);
    serialData.enviarMensajeMODO();
    sliderPWM_callback(sliderPWM, this); // mandamos el PWM visible en el slider nada mas abrir la pantalla principal
    config_GUI_lazo_abierto();
}

// Declaraciones de metodos de pantalla de bienvenida para poder definirlos despues de ambas pantallas y solucionar las dependencias circulares
void botonConectar_callback(Fl_Widget *w, void *data);
void desplegable_callback(Fl_Widget *w, void *data);
void botonActualizar_callback(Fl_Widget *w, void *data);

int main()
{
    abrirConsolaDebug();

    std::cout << "Debug iniciado\n";

    Fl_Window ventana(0, 0, 1200, 676, "Control de temperatura - Alberto Serrano Moreno");
    Fl_Wizard wizard{0, 0, ventana.w(), ventana.h()}; // widget invisible con el mismo tamaño que la ventana que nos sirve para iterar la visibilidad de sus grupos hijos

    pantallaPrincipal *pPrincipal = nullptr; //  puntero vacio por ahora. Para poder pasar la direccion de principal antes de que el objeto haya sido creado

    /* =======================
       PANTALLA(GRUPO) BIENVENIDA
       ======================= */
    Fl_Group grupoBienvenida(0, 0, ventana.w(), ventana.h());

    // TITULO
    Fl_Box tituloBienvenida(0, ventana.h() / 4, ventana.w(), 40, "Control PID de temperatura");
    tituloBienvenida.labelsize(40);
    tituloBienvenida.labelfont(FL_BOLD);

    // BOTON CONECTAR
    const int wBotonConectar = 300;
    const int hBotonConectar = 60;
    const int xBotonConectar = ventana.w() / 2 - wBotonConectar / 2;
    const int yBotonConectar = ventana.h() / 2;
    Fl_Button botonConectar{xBotonConectar, yBotonConectar, wBotonConectar, hBotonConectar, "Conectar"};
    botonConectar.labelsize(24);
    botonConectar.labelfont(FL_BOLD);
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
    desplegableCOM.align(FL_ALIGN_BOTTOM);

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
        dataConectar->pWizard->next();

        // COMUNICACIONES CON LA PANTALLA PRINCIPAL
        dataConectar->pPrincipal->setup();
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
    // Fl_Output *pTextoCOM = static_cast<Fl_Output *>(data); // pequeño texto para observar la variable global del puerto elegido

    int indice = pDesplegable->value();
    if (indice == -1) // nada seleccionado
    {
        return;
    }

    string puertoSeleccionado = pDesplegable->mvalue()->label(); // mvalue() devuelve el objeto menu item seleccionado. value() solo devuelve un entero del indice seleccionado
    // pTextoCOM->value(puertoSeleccionado.c_str());
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

double calcularCelsius(int adcValue)
{
    // 1. Configuración del ADC (10 bits para STM32 en framework Arduino)
    const double ADC_MAX = 1023.0;

    // 2. Parámetros del hardware (Resistencia R3 en el esquemático)
    const double RESISTENCIA_FIJA = 10000.0; // 10k Ohms

    // 3. Parámetros del NTC (según datasheet)
    const double R0 = 10000.0;  // Resistencia a 25°C
    const double T0 = 298.15;   // 25°C en Kelvin (273.15 + 25)
    const double BETA = 3380.0; // Constante B (25/50°C)

    // Validación de seguridad para evitar divisiones por cero o logaritmos negativos
    if (adcValue <= 0 || adcValue >= (int)ADC_MAX)
    {
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