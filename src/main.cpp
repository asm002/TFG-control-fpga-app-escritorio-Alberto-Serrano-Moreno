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

#define PERIODO_INTERRUPCION_PERIODICA 0.05 // en segundos. Actualmente 50ms. Deberia poder subirse sin problema hasta 50ms y los datos siguen llegando a la misma velocidad (cada 100ms)
#define VENTANA_DEBUG                       // comentar esta linea para que no se lanze la ventana terminal extra

#define KP0 8.0
#define KI0 0.6
#define KD0 2.0
#define REF0 1.0

// uso esta ventana extra para observar los mensajes que mando al micro
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
string puertoString = "";                // nombre del puerto abierto
vector<string> puertosGuardados;         // seria mejor que en vez de global, fuese una variable en la misma clase que el desplegable de puertos
std::unique_ptr<SerialPort> puertoSerie; // (NO ES UN PUNTERO, ES UN OBJETO QUE CONTIENE UN PUNTERO ENTRE OTRAS COSAS): puntero inteligente global para el objeto puerto serie, que se construye en el callback de boton conectar pero el objeto no vive en el ambito del callback (porque sino, se destruiria al finalizar el callback)

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
            // CloseHandle(_serialHandle);   // no hay que hacerlo aqui ni en ningun otro sitio, porque ya se encarga el destructor
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
            // CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo configurar el puerto serie.");
        }

        COMMTIMEOUTS t{};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutConstant = 0;
        t.ReadTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(_serialHandle, &t))
        {
            // CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo configurar los timeouts del puerto serie.");
        }
        // BORRAR LOS BUFFERS INTERNOS DE WINDOWS PARA EL PUERTO. Lo hago para no acumular basura anterior si hubiera
        // PURGE_RXCLEAR: Borra el buffer de entrada (lectura)
        // PURGE_TXCLEAR: Borra el buffer de salida (escritura)
        PurgeComm(_serialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }

    ~SerialPort()
    {
        CloseHandle(_serialHandle);
    }

    void send(int value) // no lo uso en el proyecto, pero lo dijo porque lo hicimos en clase
    {
        std::string data = std::to_string(value) + "\n"; // mandamos una cadena de caracteres (string, formato ASCII) terminada en \n (delimitador). Por ejemplo: "123\n"
        DWORD bytesWritten{0};                           // tipo de datos de windows (unsigned 32 bits). WriteFile escribre en esta variable cuantos bytes se han enviado

        if (!WriteFile(_serialHandle, data.c_str(), data.size(), &bytesWritten, NULL)) // aqui se produce el envio real. serialHandle representa al puerto abierto. NULL: operacion sincrona
        {
            // CloseHandle(_serialHandle);
            throw std::runtime_error("No se pudo enviar el dato por el puerto serie.");
        }
    }

    void sendString(const std::string &data)
    {
        DWORD bytesWritten{0};
        if (!WriteFile(_serialHandle, data.c_str(), data.size(), &bytesWritten, NULL))
        {
            // CloseHandle(_serialHandle);
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
            // CloseHandle(_serialHandle);
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

    int read() // tampoco lo uso
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

    static std::vector<std::string> buscar_puertos_serie() // mira en los registros de windows y devuelve nombres tipo "COMx"
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

class SerialData // clase que encapsula todo lo relacionado con los mensajes enviados y recibidos por el puerto serie. Tambien almacena los datos graficos
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

    struct DatosGraficos // todos los datos que se grafican provienen del micro. No se hace ningun calculo intermedio (aparte del escalado para la grafica)
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
        char buffer[64];                                    // creamos un array de caracteres con tamaño suficiente para el mensaje
        snprintf(buffer, sizeof(buffer),                    // snprintf es como printf pero no escribe en consola, si no en un char[]. De esta manera controlamos perfectamente el tamaño y formato el mensaje que se enviara
                 "3-PID %+06.2f %+06.2f %+06.2f %+06.2f\n", // PID <kp.00> <ki.00> <kd.00> <consigna.00>
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
                 "1-MODO %01d\n", // MODO <0 = lazo abierto || 1 = lazo cerrado>
                 modoInt);
        puertoSerie->sendString(buffer);
    }

    void enviarMensajePWM(int pwm)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer),
                 "2-PWM %04d\n", // PWM <valor>
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
    struct Serie // cada serie es una curva con un color
    {
        std::vector<float> buffer; // cada serie tiene un buffer, donde se guardan todos sus puntos
        Fl_Color color;
        float minValor; // cada serie tiene su escala
        float maxValor;
        std::string nombre; // nombre para luego generar una leyenda

        Serie(const std::string &n, Fl_Color c, int capacidad, float minV, float maxV)
            : nombre(n), color(c), minValor(minV), maxValor(maxV)
        {
            buffer.reserve(capacidad); // se reserva memoria antes de añadir elementos. Podria eliminarse pero tendria peor rendimiento por las realocaciones
            // esto NO hace que .size() tenga un valor inicial igual a capacidad. Una cosa es el tamaño (elementos en el vector) y otra la capacidad en memoria (elementos que puede haber)
        }
    };

    std::vector<Serie> series; // vector donde se iran almacenando todas las series
    size_t maxPuntos;          // puntos que tendra la grafica, segun su ancho

    Fl_Offscreen offscreen = 0; // buffer offscreen para evitar parpadeos (se dibuja todo en memoria y luego se actualiza la pantalla de una vez)

    static constexpr int MARGEN_Y = 8; // para que los valores extremos no se dibujen justo en el borde de la grafica y no se vean

    void dibujarLeyenda()
    {
        // esquina donde empieza la leyenda
        int x0 = 8;
        int y0 = 8;

        int spacing = 16; // espacio entre cada elemento de la leyenda

        for (const Serie &s : series)
        {
            // cuadrado para indicar el color que tiene la serie
            fl_color(s.color);
            fl_rectf(x0, y0 + 4, 10, 10);

            // texto (no es un fl_output ni fl_box, se puede dibujar texto directamente)
            fl_color(FL_BLACK);
            char txt[64]; // buffer temporal donde construimos el string con formato fprintf
            snprintf(txt, sizeof(txt), "%s [ %g --> %g ]",
                     s.nombre.c_str(), s.minValor, s.maxValor);

            fl_draw(txt, x0 + 16, y0 + 14);

            y0 += spacing;
        }
    }

public:
    Grafica(int x, int y, int w, int h, const char *label = nullptr)
        : Fl_Widget(x, y, w, h, label), maxPuntos(w)
    {
        box(FL_FLAT_BOX); // recuadro alrededor de la grafica

        offscreen = fl_create_offscreen(w, h); // crea el buffer en memoria para dibujar en memoria antes de mandar el dibujo
    }

    ~Grafica()
    {
        if (offscreen)
            fl_delete_offscreen(offscreen); // libera la memoria del buffer offscreen
    }

    int añadirSerie(const std::string &nombre, Fl_Color color, float minV, float maxV)
    {
        series.emplace_back(nombre, color, maxPuntos, minV, maxV); // emplace_back llama al constructor del objeto (Serie) y añade el objeto al final del vector
        return series.size() - 1;                                  // para devolver el indice de la serie (primer indice es 0)
    }

    void añadirDato(int idSerie, float valor)
    {
        if (idSerie < 0 || idSerie >= (int)series.size()) // si el id no es valido no hacemos nada
            return;

        Serie &s = series[idSerie]; // accedemos por referencia a la serie correspondiente al id (podria ser puntero en vez de referencia, da igual)

        if (s.buffer.size() >= maxPuntos)     // si ya hemos llenado la grafica:
            s.buffer.erase(s.buffer.begin()); // borramos el primer elemento

        s.buffer.push_back(valor); // añadimos al final el nuevo dato

        redraw(); // redibujamos la grafica porque hay un dato nuevo
    }

    void draw() override
    {
        if (!offscreen) // barrera de seguridad por si no funciona el buffer de memoria
            return;

        // ==========================
        // DIBUJO EN MEMORIA
        // ==========================
        fl_begin_offscreen(offscreen); // empezar a dibujar en memoria

        fl_push_clip(0, 0, w(), h()); // para impedir dibujar fuera del area que se ve

        // Fondo
        fl_color(FL_WHITE);
        fl_rectf(0, 0, w(), h());

        // linea horizontal central
        fl_color(fl_rgb_color(200, 200, 200));
        int yMedio = h() / 2;
        fl_line(0, yMedio, w(), yMedio);

        // bucle para dibujar cada serie
        for (const Serie &s : series)
        {
            if (s.buffer.size() < 2) // si hay menos de 2 puntos, no hacemos nada todavia
                continue;

            fl_color(s.color);          // cambiamos al color de la serie
            fl_line_style(FL_SOLID, 2); // linea de grosor 2 continua

            float rango = s.maxValor - s.minValor;
            if (rango == 0) // evitar division por cero
                rango = 1;

            for (size_t i = 1; i < s.buffer.size(); ++i) // recorremos el buffer desde el segundo punto (i=1) hasta el ultimo (i= size - 1)
            {
                int x0 = i - 1; // punto X anterior
                int x1 = i;     // punto X actual

                float v0 = s.buffer[i - 1]; // valor anterior
                float v1 = s.buffer[i];     // valor actual

                // que tanto por uno corresponde el valor del punto respecto de la escala de la serie?
                // si es ADC con [500 -> 1000] y tenemos v = 600 :
                // (600 - 500) / (1000 - 500) = 20%
                float proporcionV0 = (v0 - s.minValor) / rango;
                float proporcionV1 = (v1 - s.minValor) / rango;

                // como el origen de y es en la parte superior en vez de abajo, el punto inicial y0 es y0 = h()
                // como la coordenada y crece hacia abajo, yMin = h() ; yMax = 0
                // por tanto, a yMin hay que restarle la proporcion de h

                // y = h() - proporcion*h()

                // para tener un margen superior e inferior de manera que los valores maximos y minimos no se pinten debajo del borde (y no se vean)
                // yMin = h() - MARGEN
                // el alto total ya no es h(), es h() menos 2 veces el margen
                // nuevo alto = h() - 2*MARGEN

                // y_con_margenes = [h() - MARGEN] - proporcion*[h() - 2*MARGEN]

                int y0 = (h() - MARGEN_Y) - proporcionV0 * (h() - 2 * MARGEN_Y);
                int y1 = (h() - MARGEN_Y) - proporcionV1 * (h() - 2 * MARGEN_Y);

                fl_line(x0, y0, x1, y1);
            }
        }

        fl_line_style(0); // deja el estilo de linea como estaba en su valor por defecto

        // marco por encima de las curvas
        fl_color(FL_BLACK);
        fl_rect(0, 0, w(), h());

        fl_pop_clip(); // deshace el push clip para permitir dibujar fuera del marco que dijimos

        dibujarLeyenda(); // leyenda por encima de las curvas tambien

        fl_end_offscreen(); // terminar de dibujar en memoria

        // ==========================
        // COPIA DE MEMORIA A PANTALLA
        // ==========================
        fl_copy_offscreen(x(), y(), w(), h(), offscreen, 0, 0);
    }
};

class pantallaPrincipal : public Fl_Group
{
private:
    // Todas las funciones de callback deben ser static, para ser funciones de clase y no de objeto, y no llevar implicitamente el puntero this al objeto propio, ya que la firma que acepta FLTK debe ser la que es y no llevar nada extra
    static void botonCerrar_callback(Fl_Widget *w, void *data)
    {
        Fl_Window *ventana = static_cast<Fl_Window *>(data);
        ventana->hide();
    }

    static void sliderPWM_callback(Fl_Widget *w, void *data)
    {
        Fl_Hor_Value_Slider *pSlider = static_cast<Fl_Hor_Value_Slider *>(w);
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data);

        int valorDelSlider = pSlider->value();
        self->serialData.enviarMensajePWM(valorDelSlider); // sí, se manda un mensaje cada vez que cambia el valor, lo cual no es eficiente, seria mejor mandarlo al soltar el slider, pero lo prefiero asi porque me gusta el efecto de cambio continuo y la velocidad de transmision y de lectura del micro es suficiente. Tampoco se envia nada mas mientras, por lo que no se compromete ninguna informacion
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

            self->textoTemp->value(static_cast<double>(temperatura_recibida));
            self->textoConsigna->value(consigna_recibida);
            self->textoError->value(error_recibido);
            self->textoPWM->value(pwm_recibido);

            self->graficas->añadirDato(self->idTemp, temperatura_recibida);
            self->graficas->añadirDato(self->idConsigna, consigna_recibida);
            self->graficas->añadirDato(self->idError, error_recibido);
            self->graficas->añadirDato(self->idPWM, pwm_recibido);
        }
        Fl::repeat_timeout(PERIODO_INTERRUPCION_PERIODICA, timeout_callback, data); // REPROGRAMAR TIMER
    }

    void config_GUI_lazo_cerrado()
    {
        rbLazoCerrado->value(1);
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

    void lazo_cerrado()
    {
        config_GUI_lazo_cerrado();
        serialData.actualizarModo(SerialData::Modo::LAZO_CERRADO);
        serialData.enviarMensajeMODO();
        actualizarPID_callback(botonActualizarPID, this); // para mandar los valores visibles mantalla nada mas cambiar a lazo cerrado, sin tener que pulsar el boton la primera vez
    }

    void lazo_abierto()
    {
        config_GUI_lazo_abierto();
        serialData.actualizarModo(SerialData::Modo::LAZO_ABIERTO);
        serialData.enviarMensajeMODO();
        sliderPWM_callback(sliderPWM, this); // para mandar el valor visible en el slider inmediatamente, sin tener que moverlo la primera vez
    }

    static void radio_callback(Fl_Widget *w, void *data) // accion de los radio buttons, para conmutar entre lazo abierto y cerrado
    {
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data);
        Fl_Round_Button *rb = static_cast<Fl_Round_Button *>(w);
        if (self->rbLazoCerrado->value() == 1)
        {
            // LAZO CERRADO
            self->lazo_cerrado();
        }
        else
        {
            // LAZO ABIERTO
            self->lazo_abierto();
        }
    }

    void configurarPanelControl(const int wPanel, const int hPanel, const int xPanel, const int yPanel, const int margenPanel); // prototipo

    static void actualizarPID_callback(Fl_Widget *w, void *data)
    {
        pantallaPrincipal *self = static_cast<pantallaPrincipal *>(data);

        // 1. Forzar que los inputs se ajusten a los límites de su método bounds()
        double kp_valido = self->inputKp->clamp(self->inputKp->value());
        double ki_valido = self->inputKi->clamp(self->inputKi->value());
        double kd_valido = self->inputKd->clamp(self->inputKd->value());

        // 2. Actualizar visualmente el texto del input si el usuario escribió un exceso
        self->inputKp->value(kp_valido);
        self->inputKi->value(ki_valido);
        self->inputKd->value(kd_valido);

        self->serialData.actualizarDatosPID(kp_valido, ki_valido, kd_valido, self->sliderREF->value());
        self->serialData.enviarMensajePID(); // manda por el puerto serie el PID y la consigna
    }

public:
    // uso atributos puntero para poder crear los objetos en el cuerpo del constructor (como me gusta mas a mi para tener encima las const int de dimensionado y tener todo junto)
    // en consecuencia, tengo que usar "new", pero no tengo que preocuparme de "delete" porque FLTK gestiona automaticamente la destruccion de los hijos
    Fl_Window *ventana;
    Fl_Wizard *wizard;

    Fl_Box *titulo;
    Fl_Button *botonCerrar;

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

    SerialData serialData; // lo que no son widgets de FLTK, lo creo sin puntero

    Fl_Group *grupoColumnaDatosGraficos;
    Fl_Pack *columnaDatosGraficos;
    Fl_Output *textoTemp;
    Fl_Output *textoConsigna;
    Fl_Output *textoError;
    Fl_Output *textoPWM;

    Grafica *graficas;
    int idTemp, idConsigna, idError, idPWM;

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

        // BOTON CERRAR (cierra el programa como la X de la ventana)
        const int wbotonCerrar = 100;
        const int hbotonCerrar = 40;
        const int xbotonCerrar = 15;
        const int ybotonCerrar = 15;
        botonCerrar = new Fl_Button{xbotonCerrar, ybotonCerrar, wbotonCerrar, hbotonCerrar, "Cerrar"};
        botonCerrar->labelsize(20);
        botonCerrar->labelfont(FL_BOLD);
        botonCerrar->callback(botonCerrar_callback, this->parent()->parent());

        configurarPanelControl(300,
                               v->h() - 2 * (ybotonCerrar + hbotonCerrar + 30),
                               xbotonCerrar,
                               ybotonCerrar + hbotonCerrar + 30,
                               10);

        // COLUMNA DE VALORES DE LAS VARIABLES GRAFICADAS
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
                                                 panelControl->h()};
        grupoColumnaDatosGraficos->box(FL_THIN_UP_BOX);
        columnaDatosGraficos = new Fl_Pack(grupoColumnaDatosGraficos->x() + margen,
                                           grupoColumnaDatosGraficos->y() + (grupoColumnaDatosGraficos->h()) / 2 - (4 * hWidgetsColumnaDatosGraficos + 3 * spacingWidgetsColumnaDatosGraficos) / 2,
                                           100,
                                           0);
        columnaDatosGraficos->type(Fl_Pack::VERTICAL);
        columnaDatosGraficos->spacing(spacingWidgetsColumnaDatosGraficos); // espacio vertical entre widgets

        // TEXTO DE TEMPERATURA LEIDA
        textoTemp = new Fl_Output{0, 0, 0, hWidgetsColumnaDatosGraficos, "Temperatura"};
        formatoWidgetsColumnaDatosGraficos(textoTemp);

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
        idTemp = graficas->añadirSerie("TEMPERATURA", FL_GREEN, -99.9, +99.9);
        idConsigna = graficas->añadirSerie("CONSIGNA", FL_BLUE, -99.9, +99.9);
        idError = graficas->añadirSerie("ERROR", FL_RED, -99.9, +99.9);
        idPWM = graficas->añadirSerie("PWM", FL_MAGENTA, 0.0, 1023.0);

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
    sliderPWM->bounds(0, 1023);
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
    inputKp->step(0.01);
    inputKp->bounds(0, 99.99);

    inputKi->value(KI0);
    inputKi->step(0.01);
    inputKi->bounds(0, 99.99);

    inputKd->value(KD0);
    inputKd->step(0.01);
    inputKd->bounds(0, 99.99);

    yElementosPanel += 50;
    sliderREF = new Fl_Hor_Value_Slider{xElementosPanel,
                                        yElementosPanel,
                                        wElementosPanel,
                                        30,
                                        "Consigna"};
    sliderREF->labelsize(18);
    sliderREF->textsize(14);
    sliderREF->bounds(-99.90, +99.90);
    sliderREF->step(0.1);
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
    lazo_abierto();
}

// Declaraciones de metodos de pantalla de bienvenida para poder definirlos despues de ambas pantallas y solucionar las dependencias circulares
void botonConectar_callback(Fl_Widget *w, void *data);
void desplegable_callback(Fl_Widget *w, void *data);
void botonActualizar_callback(Fl_Widget *w, void *data);

int main()
{
#ifdef VENTANA_DEBUG
    abrirConsolaDebug();
#endif

    std::cout << "Debug iniciado\n";

    Fl_Window ventana(0, 0, 1200, 676, "Control PID de temperatura en FPGA - Alberto Serrano Moreno");
    Fl_Wizard wizard{0, 0, ventana.w(), ventana.h()}; // widget invisible con el mismo tamaño que la ventana que nos sirve para iterar la visibilidad de sus grupos hijos

    pantallaPrincipal *pPrincipal = nullptr; //  puntero vacio por ahora. Para poder pasar la direccion de principal antes de que el objeto haya sido creado

    /* =======================
       PANTALLA(GRUPO) BIENVENIDA
       ======================= */
    Fl_Group grupoBienvenida(0, 0, ventana.w(), ventana.h());

    // TITULO
    Fl_Box tituloBienvenida(0, ventana.h() / 4, ventana.w(), 40, "Control PID de Temperatura en FPGA");
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

    // =========================

    wizard.end();
    ventana.end();

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
