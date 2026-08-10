// InventoryBridge.cpp
// OverlayAI - Inventory bridge utility
// Este archivo implementa la verificación del flag de lanzamiento "-insecure" y
// controla la variable global g_weaponSkinSocacheAllowed que habilita o no el
// acceso a Weapon Skin SOCache durante la sesión.
//
// Comentarios: todo está en español y detallado para facilitar auditoría y mantenimiento.

#include <windows.h>
#include <cwchar>
#include <cstring>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

// Variable global que controla si la funcionalidad relacionada a Weapon Skin SOCache
// está permitida durante la sesión actual. Por seguridad, se inicializa en 'false'.
bool g_weaponSkinSocacheAllowed = false;

// Helper: escribe un mensaje de log en un archivo local "overlayai.log" en modo append.
// Se usa std::wofstream para preservar salida en wide chars (wchar_t).
static void AppendLog(const std::wstring& message)
{
	// Construye timestamp legible
	auto now = std::chrono::system_clock::now();
	std::time_t now_c = std::chrono::system_clock::to_time_t(now);

	std::wstringstream ss;
	ss << L"[" << std::put_time(std::localtime(&now_c), L"%F %T") << L"] " << message << L"\n";

	// Abre/crea archivo en modo append y escribe la línea
	std::wofstream ofs("overlayai.log", std::ios::app);
	if (ofs)
	{
		ofs << ss.str();
		ofs.close();
	}

	// También enviar a debugger output para facilitar depuración en tiempo real
	OutputDebugStringW(ss.str().c_str());
}

// HasInsecureLaunchFlag
// ---------------------
// Comprueba si la línea de comando de la aplicación contiene exactamente el token
// -insecure (como un token separado). La búsqueda evita coincidencias parciales
// (por ejemplo, no considera "-insecurely" como válido).
//
// Lógica:
//  - Obtiene el command line wide string de Windows con GetCommandLineW().
//  - Busca la primera aparición de la subcadena "-insecure" usando wcsstr.
//  - Verifica los límites del token: carácter anterior (si existe) debe ser
//    espacio, tab o comilla, o ser el inicio de la cadena; carácter siguiente (si
//    existe) debe ser espacio, tab, comilla o fin de cadena.
//  - Si se encuentra un token válido, devuelve true.
//
// Devuelve: true si -insecure aparece como token separado, false en caso contrario.
static bool HasInsecureLaunchFlag()
{
	const wchar_t* commandLine = GetCommandLineW();
	if (!commandLine) return false; // seguridad: si no hay línea de comando, no está el flag

	constexpr const wchar_t flag[] = L"-insecure";
	const size_t flagLen = std::wcslen(flag); // longitud sin el terminador nulo

	const wchar_t* cursor = commandLine;

	// Recorremos todas las ocurrencias posibles de la subcadena
	while (const wchar_t* found = std::wcsstr(cursor, flag))
	{
		// Determinar si el token empieza en un límite (inicio o precedido por espacio/tab/\")
		bool startsToken = (found == commandLine) ||
			(found[-1] == L' ') || (found[-1] == L'\t') || (found[-1] == L'\"');

		// Carácter justo después del token (si existe)
		const wchar_t after = found[flagLen];

		// Determinar si el token termina en un límite (fin o seguido por espacio/tab/\")
		bool endsToken = (after == L'\0') || (after == L' ') || (after == L'\t') || (after == L'\"');

		if (startsToken && endsToken)
		{
			AppendLog(L"HasInsecureLaunchFlag: token '-insecure' detectado como token separado.");
			return true;
		}

		// Avanzar el cursor para buscar siguientes ocurrencias (evitar bucle infinito)
		cursor = found + 1;
	}

	AppendLog(L"HasInsecureLaunchFlag: token '-insecure' no detectado.");
	return false;
}

// InitializeInventoryBridge
// -------------------------
// Punto de entrada para inicializar la parte del InventoryBridge que depende de
// la presencia del flag -insecure. Esta función decide el valor de la variable
// global g_weaponSkinSocacheAllowed y registra el estado en el log.
void InitializeInventoryBridge()
{
	AppendLog(L"InventoryBridge iniciado.");

	// Por defecto false. Solo permitimos el SOCache si el proceso fue lanzado con -insecure.
	g_weaponSkinSocacheAllowed = HasInsecureLaunchFlag();

	// Registro detallado del estado resultante
	if (g_weaponSkinSocacheAllowed)
	{
		AppendLog(L"Weapon skin SOCache: habilitado para sesión (flag -insecure presente).");
	}
	else
	{
		AppendLog(L"Weapon skin SOCache: bloqueado para seguridad (flag -insecure ausente).");
	}
}

// Nota:
// - Este archivo está pensado para incorporarse en el proyecto OverlayAI y ser
//   llamado desde el punto de inicialización del módulo (por ejemplo, DllMain
//   o la función main() del ejecutable).
// - Los logs quedan en overlayai.log en el directorio de trabajo. Ajustar según
//   las políticas del proyecto si se desea otro destino (EventLog, syslog, etc.).
