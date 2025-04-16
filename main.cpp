#include <wx/wx.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <iomanip>
#include <regex>
#include <algorithm>

using json = nlohmann::json;

// Callback zapisywania odpowiedzi z CURL do stringa
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// Funkcja pobieraj¹ca dane z URL za pomoc¹ CURL
std::string FetchURL(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            response = "B³¹d CURL: " + std::string(curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
    }
    return response;
}

// Funkcja parsuj¹ca datê i czas w formacie DD-MM-YYYY HH:MM
std::tm parseDateTime(const std::string& s) {
    std::tm tm = {};
    if (s.size() != 16) return tm;

    tm.tm_mday = std::stoi(s.substr(0, 2));
    tm.tm_mon = std::stoi(s.substr(3, 2)) - 1;
    tm.tm_year = std::stoi(s.substr(6, 4)) - 1900;
    tm.tm_hour = std::stoi(s.substr(11, 2));
    tm.tm_min = std::stoi(s.substr(14, 2));
    tm.tm_sec = 0;

    if (tm.tm_hour < 0 || tm.tm_hour > 23 || tm.tm_min < 0 || tm.tm_min > 59) {
        tm = {};
    }

    return tm;
}

// Funkcja sprawdzaj¹ca czy string ma poprawny format daty
bool isValidDate(const std::string& s) {
    std::regex pattern("\\d{2}-\\d{2}-\\d{4} \\d{2}:\\d{2}");
    return std::regex_match(s, pattern);
}

// G³ówne okno aplikacji
class MainFrame : public wxFrame {
public:
    // Konstruktor inicjalizuj¹cy GUI
    MainFrame() : wxFrame(NULL, wxID_ANY, "Lista stacji GIOŒ", wxDefaultPosition, wxSize(800, 600)) {
        wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
        listBox = new wxListBox(this, wxID_ANY);
        mainSizer->Add(listBox, 1, wxEXPAND | wxALL, 5);

        radioSizer = new wxBoxSizer(wxHORIZONTAL);
        mainSizer->Add(radioSizer, 0, wxEXPAND | wxALL, 5);

        wxBoxSizer* dateSizer = new wxBoxSizer(wxHORIZONTAL);
        dateSizer->Add(new wxStaticText(this, wxID_ANY, "Od (DD-MM-YYYY HH:MM):"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        fromDate = new wxTextCtrl(this, wxID_ANY);
        dateSizer->Add(fromDate, 1, wxALL, 5);
        dateSizer->Add(new wxStaticText(this, wxID_ANY, "Do (DD-MM-YYYY HH:MM):"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        toDate = new wxTextCtrl(this, wxID_ANY);
        dateSizer->Add(toDate, 1, wxALL, 5);
        mainSizer->Add(dateSizer, 0, wxEXPAND | wxALL, 5);

        fetchButton = new wxButton(this, wxID_ANY, "Pobierz dane parametru");
        mainSizer->Add(fetchButton, 0, wxALL | wxALIGN_CENTER, 5);

        output = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
        mainSizer->Add(output, 1, wxEXPAND | wxALL, 5);

        graphPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 200));
        graphPanel->SetBackgroundColour(*wxWHITE);
        mainSizer->Add(graphPanel, 0, wxEXPAND | wxALL, 5);

        SetSizer(mainSizer);
        Layout();

        fetchButton->Bind(wxEVT_BUTTON, &MainFrame::OnFetchData, this);
        listBox->Bind(wxEVT_LISTBOX, &MainFrame::OnStationSelected, this);

        LoadStations();
    }

private:
    // Elementy GUI
    wxListBox* listBox;                 // Lista stacji
    wxRadioBox* parameterBox = nullptr; // Wybór parametru pomiarowego
    wxBoxSizer* radioSizer;             // Kontener dla opcji parametru
    wxTextCtrl* fromDate;               // Pole do wprowadzenia daty pocz¹tkowej
    wxTextCtrl* toDate;                 // Pole do wprowadzenia daty koñcowej
    wxTextCtrl* output;                 // Pole do wyœwietlania wyników
    wxButton* fetchButton;              // Przycisk pobierania danych
    wxPanel* graphPanel;                // Panel do rysowania wykresu
    
    // Dane
    std::vector<int> stationIds;        // Lista ID stacji
    std::vector<int> sensorIds;         // Lista ID czujników
    std::vector<std::string> paramNames;// Lista nazw parametrów

    std::vector<double> graphValues;    // Wartoœci do wykresu
    std::vector<std::string> graphLabels;// Etykiety do wykresu

    // Wczytuje listê stacji
    void LoadStations() {
        //Pobranie danych o wszystkich stacjach
        std::string data = FetchURL("https://api.gios.gov.pl/pjp-api/rest/station/findAll");
        try {
            //Parsowanie pobranych danych i umieszczenie nazwy i id stacji w wektorze stations
            json j = json::parse(data);
            std::vector<std::pair<std::string, int>> stations;
            for (const auto& station : j) {
                std::string name = station["stationName"];
                int id = station["id"];
                stations.emplace_back(name, id);
            }
            //Posortowanie alfabetycznie stacji wyœwietlanych na liœcie
            std::sort(stations.begin(), stations.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
                });
            for (const auto& station : stations) {
                stationIds.push_back(station.second);
                listBox->Append(wxString::FromUTF8(station.first));
            }
        }
        catch (...) {
            output->SetValue("B³¹d ³adowania stacji.");
        }
    }

    // Obs³uguje wybór stacji z listy
    void OnStationSelected(wxCommandEvent&) {
        //Pobranie id wybranej stacji
        int idx = listBox->GetSelection();
        if (idx < 0) return;

        //Przygotowanie zapytanie i pobranie sensorów dla wybranej stacji
        std::string url = "https://api.gios.gov.pl/pjp-api/rest/station/sensors/" + std::to_string(stationIds[idx]);
        std::string data = FetchURL(url);

        try {
            json j = json::parse(data);
            wxArrayString choices;
            sensorIds.clear();
            paramNames.clear();

            //Dodawanie do wektorów nazw parametrów dla listy RadioBox oraz id
            for (const auto& sensor : j) {
                std::string param = sensor["param"]["paramName"];
                sensorIds.push_back(sensor["id"]);
                paramNames.push_back(param);
                choices.Add(wxString::FromUTF8(param));
            }

            if (parameterBox) {
                radioSizer->Detach(parameterBox);
                parameterBox->Destroy();
            }

            //Utworzenie RadioBox z parametrami wybranego sensora
            parameterBox = new wxRadioBox(this, wxID_ANY, "Parametry", wxDefaultPosition, wxDefaultSize, choices);
            radioSizer->Add(parameterBox, 1, wxEXPAND | wxALL, 5);
            Layout();
        }
        catch (...) {
            output->SetValue("B³¹d ³adowania sensorów.");
        }
    }

    // Rysowanie wykresu w panelu graphPanel
    void DrawGraph() {
        wxClientDC dc(graphPanel);
        dc.Clear();

        // Sprawdzenie czy s¹ dane do narysowania
        if (graphValues.empty()) return;

        int width, height;
        graphPanel->GetSize(&width, &height);

        // Okreœlenie marginesów i wymiarów
        int leftMargin = 50;
        int bottomMargin = 30;
        int topMargin = 10;
        int rightMargin = 10;

        int graphWidth = width - leftMargin - rightMargin;
        int graphHeight = height - topMargin - bottomMargin;

        double maxValue = *std::max_element(graphValues.begin(), graphValues.end());
        double minValue = *std::min_element(graphValues.begin(), graphValues.end());

        double range = maxValue - minValue;
        if (range == 0) range = 1; // zabezpieczenie w wypadku 1 wartoœci

        // Obliczenie szerokoœci kolumn
        int barWidth = std::max(1, graphWidth / (int)graphValues.size());

        // Osie
        dc.DrawLine(leftMargin, topMargin, leftMargin, height - bottomMargin); // Y
        dc.DrawLine(leftMargin, height - bottomMargin, width - rightMargin, height - bottomMargin); // X

        // Podpisy osi Y
        for (int i = 0; i <= 5; ++i) {
            double val = minValue + i * range / 5;
            int y = topMargin + graphHeight - (i * graphHeight / 5);
            dc.DrawLine(leftMargin - 3, y, leftMargin, y); // kreska
            wxString label = wxString::Format("%.1f", val);
            dc.DrawText(label, 2, y - 7); // etykieta
        }

        // S³upki
        for (size_t i = 0; i < graphValues.size(); ++i) {
            double val = graphValues[i];
            int barHeight = (int)((val - minValue) / range * graphHeight);
            int x = leftMargin + i * barWidth;
            int y = topMargin + graphHeight - barHeight;

            dc.SetBrush(*wxCYAN_BRUSH);
            dc.DrawRectangle(x, y, barWidth - 1, barHeight);
        }
    }




    // Obs³uguje pobieranie i wyœwietlanie danych liczbowych  pobranych z API
    void OnFetchData(wxCommandEvent&) {
        // Sprawdzenie, czy wybrano stacjê i parametr
        int stationIdx = listBox->GetSelection();
        int paramIdx = parameterBox ? parameterBox->GetSelection() : -1;

        if (stationIdx < 0 || paramIdx < 0 || paramIdx >= (int)sensorIds.size()) {
            output->SetValue("Wybierz stacjê i parametr.");
            return;
        }

        // Walidacja formatu dat wejœciowych
        std::string fromStr = fromDate->GetValue().ToStdString();
        std::string toStr = toDate->GetValue().ToStdString();

        if (!isValidDate(fromStr) || !isValidDate(toStr)) {
            output->SetValue("WprowadŸ daty w formacie DD-MM-YYYY HH:MM.");
            return;
        }

        // Konwersja ci¹gów znaków na obiekty czasu
        std::tm fromTm = parseDateTime(fromStr);
        std::tm toTm = parseDateTime(toStr);

        std::time_t fromTime = mktime(&fromTm);
        std::time_t toTime = mktime(&toTm);

        // Sprawdzenie, czy zakres dat jest poprawny
        if (fromTime > toTime) {
            output->SetValue("Data pocz¹tkowa musi byæ wczeœniejsza ni¿ koñcowa.");
            return;
        }

        //Wyliczenie najstarszej dostêpnej daty
        std::time_t now = std::time(nullptr);
        std::tm earliestAllowed = *std::localtime(&now);
        earliestAllowed.tm_mday -= 2;
        earliestAllowed.tm_hour = 1;
        earliestAllowed.tm_min = 0;
        earliestAllowed.tm_sec = 0;
        std::time_t earliestTime = mktime(&earliestAllowed);

        if (fromTime < earliestTime) {
            output->SetValue("Dostêpne s¹ tylko dane Z ostatnich trzech dób. WprowadŸ nowszy zakres.");
            return;
        }

        // Pobranie ID czujnika i przygotowanie zapytania HTTP
        int sensorId = sensorIds[paramIdx];
        std::stringstream ss;
        ss << "Dane dla parametru: " << paramNames[paramIdx] << "\n";

        std::string url = "https://api.gios.gov.pl/pjp-api/rest/data/getData/" + std::to_string(sensorId);
        std::string data = FetchURL(url);

        //Czyszczenie poprzedniego wykresu
        graphValues.clear();
        graphLabels.clear();

        try {
            //Parsowanie i sprawdzenie czy s¹ dane
            json j = json::parse(data);
            if (!j.contains("values")) {
                output->SetValue("Brak danych pomiarowych.");
                return;
            }

            int count = 0;
            // Iteracja przez dane pomiarowe i przetwarzanie wartoœci w podanym zakresie czasowym
            for (const auto& value : j["values"]) {
                std::string fullDate = value["date"].get<std::string>();
                std::tm recordTm = parseDateTime(fullDate.substr(8, 2) + "-" + fullDate.substr(5, 2) + "-" + fullDate.substr(0, 4) + " " + fullDate.substr(11, 5));
                std::time_t recordTime = mktime(&recordTm);

                if (recordTime >= fromTime && recordTime <= toTime) {
                    if (!value["value"].is_null()) {
                        graphValues.push_back((double)value["value"]);
                        graphLabels.push_back(fullDate.substr(11, 5));
                    }
                    std::string val = value["value"].is_null() ? "brak" : std::to_string((double)value["value"]);
                    ss << fullDate.substr(0, 16) << ": " << val << "\n";
                    count++;
                }
            }
            if (count == 0) {
                ss << "Brak danych pomiarowych w wybranym zakresie czasowym.\n";
            }

        }
        catch (...) {
            ss << "B³¹d przetwarzania danych.\n";
        }

        // Wyœwietlenie wyników i narysowanie wykresu
        output->SetValue(wxString::FromUTF8(ss.str()));
        DrawGraph();
    }
};

// Klasa aplikacji g³ównej
class App : public wxApp {
    wxLocale locale;
public:
    // Inicjalizacja aplikacji
    virtual bool OnInit() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        locale.Init(wxLANGUAGE_POLISH);
        MainFrame* frame = new MainFrame();
        frame->Show(true);
        frame->SetClientSize(1000, 700);
        return true;
    }
    // Sprz¹tanie przy zamkniêciu aplikacji
    virtual int OnExit() {
        curl_global_cleanup();
        return 0;
    }
};

//Makro do inicjacji aplikacji
wxIMPLEMENT_APP(App);