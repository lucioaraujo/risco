// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include "core/BytebeatExpression.hpp"
#include "core/FormulaComposition.hpp"
#include "core/Composition.hpp"
#include "core/CompositionSeed.hpp"
#include "core/TrackGeneration.hpp"
#include "core/FormulaSeed.hpp"
#include "core/Harmony.hpp"
#include "core/FormulaLibrary.hpp"
#include "core/OutputStage.hpp"
#include "core/Sound.hpp"
#include "core/SongIO.hpp"
#include "core/SongPlayback.hpp"
#include "core/SongRender.hpp"
#include "core/HistoricalAyEngine.hpp"
#include "RiscoLogo.hpp"
#include "DollchanCatalog.hpp"
#include "SongExport.hpp"
#include "AudioSnapshotRetention.hpp"
#include "AlbumGenerationJob.hpp"
#include "AlbumRenderJob.hpp"
#include "HelpReader.hpp"
#include "RecoveryJob.hpp"
#include "ProjectLibrary.hpp"
#include "../tools/RecoveryCheck.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
juce::String utf8(const char* text) { return juce::String(juce::CharPointer_UTF8(text)); }
class AudioReadGuard {
public:
    explicit AudioReadGuard(std::atomic<unsigned>& readers) noexcept : readers_(readers) {
        readers_.fetch_add(1, std::memory_order_acq_rel);
    }
    ~AudioReadGuard() { readers_.fetch_sub(1, std::memory_order_release); }
private:
    std::atomic<unsigned>& readers_;
};
bool parseAlbumTrackProfiles(const juce::String& input, int trackCount,
                             std::array<risco::AlbumMusicProfile, risco::maxAlbumTracks>& profiles) {
    profiles.fill(risco::AlbumMusicProfile::automatic);
    auto compact = input.toUpperCase().removeCharacters(" \t\r\n").toStdString();
    if (compact.empty()) return true;
    int track = 0;
    std::size_t start = 0;
    while (start <= compact.size()) {
        const auto separator = compact.find(',', start);
        const auto end = separator == std::string::npos ? compact.size() : separator;
        if (end - start != 1 || track >= trackCount) return false;
        risco::AlbumMusicProfile profile;
        switch (compact[start]) {
            case 'O': profile = risco::AlbumMusicProfile::original; break;
            case 'R': profile = risco::AlbumMusicProfile::related; break;
            case 'C': profile = risco::AlbumMusicProfile::contrasting; break;
            case '-': profile = risco::AlbumMusicProfile::automatic; break;
            default: return false;
        }
        profiles[static_cast<size_t>(track++)] = profile;
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return true;
}
constexpr std::array<std::uint32_t, 6> graffitiColours{
    0xffff786a, 0xff61e2f0, 0xffc3eb6b,
    0xffc9a2ff, 0xffffac68, 0xffff8dc5};
constexpr std::uint32_t crossedGraffitiColour = 0xffff5f78;
constexpr int formulaEditorX = 20;
constexpr int formulaEditorWidth = 660;
struct ControlBand { int x, y, width, height; };
constexpr ControlBand seedBand{12, 51, 856, 38};
constexpr ControlBand liveBand{12, 91, 1256, 38};
constexpr ControlBand bankBand{12, 130, 1014, 36};
constexpr ControlBand formulaBand{12, 168, 1256, 70};
constexpr ControlBand gridControlsBand{12, 499, 1256, 34};
constexpr int controlHeight = 30;
constexpr int centredControlY(ControlBand band, int height = controlHeight) noexcept {
    return band.y + (band.height - height) / 2;
}
constexpr int formulaFirstRowY = formulaBand.y +
    (formulaBand.height - (2 * controlHeight + 4)) / 2;
constexpr int formulaSecondRowY = formulaFirstRowY + controlHeight + 4;

class RiscoRetroLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void setFontStyle(int style) noexcept { fontStyle = std::clamp(style, 0, 2); }
    void setUiScale(float scale) noexcept { uiScale = std::clamp(scale, 0.75f, 2.0f); }
    RiscoRetroLookAndFeel() {
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff347b9a));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3b91a4));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff4fcff));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xffe0fbff));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff16384f));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff4386a0));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0f6ff));
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xffffefd0));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff377a96));
        setColour(juce::TextEditor::textColourId, juce::Colour(0xff17384d));
    }
    juce::Font getTextButtonFont(juce::TextButton&, int height) override {
        return font(float(height) * 0.50f, juce::Font::bold);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override {
        const float compactFont = uiScale < 0.9f ? 14.0f : 15.0f;
        return font(compactFont * uiScale, juce::Font::plain);
    }
    void drawComboBox(juce::Graphics& g, int width, int height, bool buttonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override {
        juce::LookAndFeel_V4::drawComboBox(g, width, height, buttonDown,
                                           buttonX, buttonY, buttonW, buttonH, box);
        if (box.hasKeyboardFocus(true)) {
            g.setColour(juce::Colour(0xffb4f0f4));
            g.drawRoundedRectangle(juce::Rectangle<float>(0.8f, 0.8f,
                float(width) - 1.6f, float(height) - 1.6f), 3.0f, 1.6f);
        }
    }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        juce::LookAndFeel_V4::positionComboBoxText(box, label);
        label.setMinimumHorizontalScale(1.0f);
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour&, bool highlighted, bool down) override {
        const auto r = button.getLocalBounds().toFloat().reduced(1.0f);
        const auto base = button.findColour(button.getToggleState()
            ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
        g.setColour(down ? base.darker(0.35f) : highlighted ? base.brighter(0.20f) : base);
        g.fillRoundedRectangle(r, 4.0f);
        if (button.hasKeyboardFocus(true)) {
            g.setColour(juce::Colour(0xffb4f0f4));
            g.drawRoundedRectangle(r, 4.0f, 1.6f);
        }
    }
private:
    juce::Font font(float size, int style) const {
        if (fontStyle == 0) return juce::Font(juce::FontOptions(size, style));
        const auto name = fontStyle == 1 ? juce::String("DejaVu Sans Mono")
                                          : "DejaVu Sans";
        return juce::Font(juce::FontOptions(name, size, style));
    }
    int fontStyle = 1;
    float uiScale = 1.0f;
};

class HelpOverlay final : public juce::Component {
public:
    HelpOverlay() {
        setOpaque(false);
        for (int i = 0; i < 3; ++i) {
            addAndMakeVisible(topics[static_cast<size_t>(i)]);
            topics[static_cast<size_t>(i)].onClick = [this, i] { setTopic(i); };
        }
        addAndMakeVisible(closeButton);
        closeButton.onClick = [this] { setVisible(false); };
        addAndMakeVisible(body);
        setLanguage(1);
    }

    void setLanguage(int selected) {
        language = std::clamp(selected - 1, 0, 3);
        const juce::String labels[4][3] = {
            {utf8("Sobre"), utf8("Tutorial"), utf8("Aprender")},
            {"About", "Tutorial", "Learn"},
            {utf8("À propos"), utf8("Tutoriel"), utf8("Apprendre")},
            {utf8("Acerca de"), "Tutorial", "Aprender"}};
        for (int i = 0; i < 3; ++i)
            topics[static_cast<size_t>(i)].setButtonText(labels[language][i]);
        const juce::String closeLabels[] = {utf8("Fechar"), "Close", utf8("Fermer"), utf8("Cerrar")};
        closeButton.setButtonText(closeLabels[language]);
        updateBody();
    }

    void setTopic(int selected) {
        topic = std::clamp(selected, 0, 2);
        for (int i = 0; i < 3; ++i)
            topics[static_cast<size_t>(i)].setToggleState(i == topic, juce::dontSendNotification);
        updateBody();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xe608101b));
        const auto panel = getLocalBounds().toFloat().reduced(92.0f, 72.0f);
        g.setColour(juce::Colour(0xff101b2d));
        g.fillRoundedRectangle(panel, 12.0f);
        g.setColour(juce::Colour(0xff347d98));
        g.drawRoundedRectangle(panel.reduced(0.5f), 12.0f, 1.5f);
        g.setColour(juce::Colour(0xff55d6e8));
        g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 22.0f, juce::Font::bold)));
        const juce::String headings[] = {
            utf8("RISCO  /  CENTRAL DE AJUDA"),
            "RISCO  /  HELP CENTER",
            utf8("RISCO  /  CENTRE D’AIDE"),
            utf8("RISCO  /  CENTRO DE AYUDA")};
        g.drawText(headings[language], juce::roundToInt(panel.getX() + 26.0f),
                   juce::roundToInt(panel.getY() + 20.0f),
                   juce::roundToInt(panel.getWidth() - 180.0f), 34,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff28516c));
        g.drawHorizontalLine(juce::roundToInt(panel.getY() + 62.0f),
                             panel.getX() + 24.0f, panel.getRight() - 24.0f);
    }

    void resized() override {
        const auto panel = getLocalBounds().reduced(92, 72);
        const int x = panel.getX() + 24;
        const int y = panel.getY() + 76;
        for (int i = 0; i < 3; ++i)
            topics[static_cast<size_t>(i)].setBounds(x + i * 142, y, 132, 34);
        closeButton.setBounds(panel.getRight() - 112, panel.getY() + 18, 88, 30);
        body.setBounds(panel.getX() + 24, y + 48, panel.getWidth() - 48,
                       panel.getHeight() - 142);
    }

private:
    void updateBody() {
        static const juce::String copy[3][4] = {
            {
                utf8("Risco é um instrumento generativo da família RASGO, criado por Lúcio de Araújo. Escrita, desenho, gestos e funções matemáticas entram como matéria para compor e transformar som.\n\nA performance pode guardar funções e desenhos em células, organizá-los na grade e retomá-los ou variá-los. O Bytebeat direto calcula uma expressão a partir do contador contínuo t e trabalha em 8 bits; a seleção 8/16 bits controla a resolução do motor de notas.\n\nO projeto está em desenvolvimento. Consulte TAREFAS.md para o estado e as limitações atuais."),
                utf8("Risco is a generative instrument from the RASGO family, created by Lúcio de Araújo. Writing, drawing, gestures and mathematical functions become material for composing and transforming sound.\n\nA performance can store functions and drawings in cells, arrange them on the music grid, then recall or vary them. Direct Bytebeat evaluates an expression from the continuous sample counter t and runs at 8 bits; the 8/16-bit selector controls the note engine's audio resolution.\n\nThe project is in development. See TAREFAS.md for current status and limitations."),
                utf8("Risco est un instrument génératif de la famille RASGO, créé par Lúcio de Araújo. Écriture, dessin, gestes et fonctions mathématiques deviennent une matière pour composer et transformer le son.\n\nLa performance peut conserver fonctions et dessins dans des cellules, les placer dans la grille musicale, puis les rappeler ou les faire varier. Le Bytebeat direct évalue une expression à partir du compteur continu t et fonctionne en 8 bits ; le sélecteur 8/16 bits règle la résolution audio du moteur de notes.\n\nLe projet est en développement. Consultez TAREFAS.md pour connaître son état et ses limites."),
                utf8("Risco es un instrumento generativo de la familia RASGO, creado por Lúcio de Araújo. La escritura, el dibujo, los gestos y las funciones matemáticas se convierten en materia para componer y transformar sonido.\n\nLa interpretación puede guardar funciones y dibujos en células, organizarlos en la cuadrícula musical y recuperarlos o variarlos. Bytebeat directo evalúa una expresión a partir del contador continuo t y funciona a 8 bits; el selector de 8/16 bits controla la resolución de audio del motor de notas.\n\nEl proyecto está en desarrollo. Consulte TAREFAS.md para conocer su estado y sus límites.")
            },
            {
                utf8("1. No quadro ENTRADA ÚNICA, digite um texto ou desenhe. Use Ouvir texto/desenho para escutar a entrada.\n2. No desenho, arraste até 12 traços. A altura e a inclinação dos pontos orientam as notas; a seed escolhe tonalidade e progressão.\n3. Para levar material à composição, escolha Stem: Melodia, Harmonia, Baixo, Ritmo ou Textura. Dê nome à célula e use Guardar célula. Com AUTO ligado, Inserir célula ou clicar numa área vazia escolhe tempo e canal; o clique define a referência temporal. Desligado, o seletor Stem e o tempo escolhido são manuais. Na escuta ao vivo, AUTO desenvolve as cinco camadas.\n4. Arraste uma célula na horizontal para mudar o tempo e na vertical para trocar de canal. Ajuste duração e repetições; Shift+arraste marca um loop.\n5. Tocar música executa a grade; o marcador âmbar mostra a posição. Início reinicia; REC inclui novas gerações; BPM e MASTER ajustam andamento e volume.\n\nRitmo reúne ataques curtos; Textura reúne camadas sustentadas. Os cinco canais têm volume, pan, mute, solo e efeitos próprios."),
                utf8("1. In ENTRADA ÚNICA, type text or draw. Use Play text/drawing to hear the input.\n2. A drawing can contain up to 12 strokes. Point height and slope guide the notes; its seed selects a key and progression.\n3. To place material in the composition, choose Stem: Melody, Harmony, Bass, Rhythm or Texture. Name the cell and choose Store cell. With AUTO on, Insert cell or clicking an empty grid area chooses time and lane; your click gives the timing reference. When off, Stem and time are manual. In live listening, AUTO develops all five layers.\n4. Drag a cell horizontally to change its time and vertically to move it between channels. Set length and repeats; Shift+drag marks a loop.\n5. Play song runs the grid; the amber marker shows its position. Restart returns to the beginning; REC includes new generations; BPM and MASTER set tempo and volume.\n\nRhythm carries short attacks; Texture carries sustained layers. All five channels have independent volume, pan, mute, solo and effects."),
                utf8("1. Saisissez un texte ou dessinez dans ENTRADA ÚNICA. Utilisez Écouter texte/dessin pour entendre l’entrée.\n2. Un dessin peut contenir jusqu’à 12 traits. La hauteur et l’inclinaison guident les notes ; la seed choisit tonalité et progression.\n3. Pour placer le matériau, choisissez Stem : Mélodie, Harmonie, Basse, Rythme ou Texture. Nommez la cellule et choisissez Guardar célula. Avec AUTO, Inserir célula ou un clic dans une case vide choisit le temps et la piste ; le clic sert de repère temporel. Sans AUTO, Stem et temps restent manuels. En écoute directe, AUTO développe les cinq couches.\n4. Faites glisser une cellule horizontalement pour changer son temps et verticalement pour changer de canal. Réglez durée et répétitions ; Maj+glisser marque une boucle.\n5. Tocar música joue la grille. Rythme porte des attaques courtes ; Texture porte des couches soutenues. Les cinq canaux disposent de volume, pan, mute, solo et effets indépendants."),
                utf8("1. Escriba texto o dibuje en ENTRADA ÚNICA. Use Oír texto/dibujo para escuchar la entrada.\n2. Un dibujo puede tener hasta 12 trazos. La altura y la inclinación guían las notas; la seed elige tonalidad y progresión.\n3. Para poner material en la composición, elija Stem: Melodía, Armonía, Bajo, Ritmo o Textura. Nombre la célula y pulse Guardar célula. Con AUTO activo, Insertar célula o un clic en un área vacía elige tiempo y canal; el clic sirve como referencia temporal. Sin AUTO, Stem y tiempo son manuales. En la escucha en vivo, AUTO desarrolla las cinco capas.\n4. Arrastre una célula horizontalmente para cambiar el tiempo y verticalmente para cambiar de canal. Ajuste duración y repeticiones; Mayús+arrastrar marca un loop.\n5. Tocar música reproduce la cuadrícula. Ritmo lleva ataques cortos; Textura lleva capas sostenidas. Los cinco canales tienen volumen, pan, mute, solo y efectos independientes.")
            },
            {
                utf8("Bytebeat calcula valores de muestra directamente. t es el contador de muestras: una fórmula relaciona esse tiempo con operadores como +, *, >>, &, | y ^.\n\nExemplo para experimentar:\nt * ((t >> 5) | (t >> 8))\n\nOs deslocamentos agrupam mudanças em escalas temporais diferentes; a combinação cria pulsos, alturas e timbres. Pequenas alterações de constante ou operador podem mudar bastante o resultado.\n\nUma função guardada é também um objeto de composição: pode ser repetida, colocada em outra posição ou recombinada. Derivadas, integrais e sistemas dinâmicos ainda são pesquisa futura no Risco; o editor atual aceita a gramática Bytebeat descrita em README.md."),
                "Bytebeat computes sample values directly. t is the sample counter: an expression relates this time to operators such as +, *, >>, &, | and ^.\n\nTry this expression:\nt * ((t >> 5) | (t >> 8))\n\nBit shifts group changes at different time scales; combining them creates pulses, pitches and timbres. Small changes to a constant or operator can produce a very different result.\n\nA saved function is also a compositional object: it can be repeated, placed elsewhere or recombined. Derivatives, integrals and dynamic systems are future research in Risco; the editor currently accepts the Bytebeat grammar documented in README.md.",
                utf8("Le Bytebeat calcule directement les valeurs des échantillons. t est le compteur d’échantillons : une expression relie ce temps à des opérateurs comme +, *, >>, &, | et ^.\n\nEssayez cette expression :\nt * ((t >> 5) | (t >> 8))\n\nLes décalages de bits regroupent les changements sur différentes échelles temporelles ; leur combinaison crée pulsations, hauteurs et timbres. Une petite modification de constante ou d’opérateur peut changer fortement le résultat.\n\nUne fonction enregistrée devient aussi un objet de composition : elle peut être répétée, replacée ou recombinée. Dérivées, intégrales et systèmes dynamiques restent des recherches futures dans Risco ; l’éditeur accepte actuellement la grammaire Bytebeat décrite dans README.md."),
                utf8("Bytebeat calcula directamente valores de muestra. t es el contador de muestras: una expresión relaciona ese tiempo con operadores como +, *, >>, &, | y ^.\n\nPruebe esta expresión:\nt * ((t >> 5) | (t >> 8))\n\nLos desplazamientos de bits agrupan cambios en distintas escalas temporales; su combinación crea pulsos, alturas y timbres. Un pequeño cambio de constante u operador puede cambiar mucho el resultado.\n\nUna función guardada también es un objeto compositivo: puede repetirse, colocarse en otro lugar o recombinarse. Derivadas, integrales y sistemas dinámicos son investigación futura en Risco; el editor acepta ahora la gramática Bytebeat descrita en README.md.")
            }
        };
        static const juce::String tutorial[4] = {
                utf8("FLUXO: DE ONDE VEM O SOM?\n\nTEXTO ou DESENHO ── Ouvir texto/desenho ──> SOM AO VIVO (fora da grade)\n       └── Guardar célula ──> BANCO ── Inserir célula ──> GRADE\n\nTEXTO / DESENHO / NÚMERO ── origem do seed ── Gerar + tocar\n       └──> NOVA FÓRMULA BYTEBEAT ──> som ao vivo\n                    └──> guardada no BANCO + colocada na GRADE\n\nFÓRMULA DIGITADA ── Compilar fórmula ── Ouvir fórmula (ao vivo)\n       ├── Guardar célula ──> BANCO; com REC, também entra na GRADE\n       └── Fórmula → notas ── pré-escuta; Guardar frase ──> célula no BANCO + GRADE\n\nBANCO = materiais guardados nesta música. GRADE = sequência de células que forma a música. Material no banco não toca na música completa até entrar na grade.\n\nPARA COMPOR: comece com uma célula curta; ouça a prévia, coloque-a na grade e ouça a música. Repita ou varie a célula para criar reconhecimento; acrescente outra para resposta ou contraste. Use BPM para mudar o pulso e arraste os blocos para escolher início e duração. Tocar música executa a grade; Salvar grava banco e grade no arquivo da performance. Salvar e Abrir abrem o seletor do sistema: escolha pasta e nome do arquivo; Salvar acrescenta .risco. Não há salvamento automático; use Salvar antes de fechar. O banco da música começa vazio em nova abertura; o repositório de fórmulas é local, persistente e independente. As cores da tela indicam ao vivo, gerar, banco e grade."),
            utf8("QUICK START\n\nChoose the destination in Stem: Melody, Harmony, Bass, Rhythm or Texture. Type or draw, name the cell, choose Store cell, then click a row in the grid. Drag vertically to change channel and horizontally to change time. Rhythm carries short attacks; Texture carries sustained layers in a full track. All five channels have their own mixer and effects.") ,
            utf8("PRISE EN MAIN\n\nChoisissez la destination dans Stem : Mélodie, Harmonie, Basse, Rythme ou Texture. Écrivez ou dessinez, nommez la cellule, choisissez Guardar célula puis cliquez sur une ligne de la grille. Glissez verticalement pour changer de canal et horizontalement pour changer le temps. Rythme porte les attaques courtes ; Texture porte les couches soutenues. Les cinq canaux ont leur mixage et leurs effets.") ,
            utf8("INICIO RÁPIDO\n\nElija el destino en Stem: Melodía, Armonía, Bajo, Ritmo o Textura. Escriba o dibuje, nombre la célula, pulse Guardar célula y haga clic en una fila de la cuadrícula. Arrastre verticalmente para cambiar de canal y horizontalmente para cambiar el tiempo. Ritmo lleva ataques cortos; Textura lleva capas sostenidas. Los cinco canales tienen su propia mezcla y efectos.")
        };
        juce::String text = topic == 1 ? tutorial[language] : copy[topic][language];
        if (topic == 2) {
            static const char* concepts[] = {
                "BYTEBEAT — A fórmula calcula amostras diretamente a partir de t. Esse contador avança durante a execução. Operadores como >>, &, | e ^ criam relações entre padrões temporais. A escuta direta usa 8 bits.\n\nFÓRMULA COMO PARTITURA — Fórmula → notas interpreta valores como alturas, durações, acentos e pausas. Escala e Tom escolhem o campo de alturas. Encadear aproxima as oitavas; Cadenciar resolve a última nota na tônica.\n\nBANCO E GRADE — O banco guarda materiais. A grade organiza quando e em qual stem eles tocam. Uma célula no banco só participa da música completa depois de ser inserida na grade.\n\nIDENTIDADE E VARIAÇÃO — A seed torna a geração reproduzível. Variar frase desenvolve uma origem em seções. Guardar cria uma célula; Atualizar modifica todas as ocorrências da célula selecionada, com desfazer/refazer do banco.",
                "BYTEBEAT — The formula calculates samples directly from t. This counter advances during playback. Operators such as >>, &, | and ^ relate patterns at different time scales. Direct playback uses 8 bits.\n\nFORMULA AS A SCORE — Fórmula → notas interprets values as pitches, durations, accents and rests. Scale and Tom choose the pitch field. Encadear brings octaves closer; Cadenciar resolves the last note to the tonic.\n\nBANK AND GRID — The bank stores materials. The grid sets when and on which stem they play. A bank cell only joins the full song after insertion into the grid.\n\nIDENTITY AND VARIATION — The seed makes generation repeatable. Variar frase develops a source in sections. Guardar creates a cell; Atualizar changes all occurrences of the selected cell, with bank undo/redo.",
                "BYTEBEAT — La formule calcule les échantillons directement à partir de t. Ce compteur avance pendant la lecture. Les opérateurs >>, &, | et ^ relient des motifs à différentes échelles de temps. La lecture directe utilise 8 bits.\n\nFORMULE COMME PARTITION — Fórmula → notas interprète les valeurs comme hauteurs, durées, accents et silences. La gamme et Tom choisissent le champ de hauteurs. Encadear rapproche les octaves ; Cadenciar résout la dernière note sur la tonique.\n\nBANQUE ET GRILLE — La banque conserve les matériaux. La grille fixe quand et sur quel stem ils jouent. Une cellule de la banque rejoint le morceau après insertion dans la grille.\n\nIDENTITÉ ET VARIATION — La seed rend la génération reproductible. Variar frase développe une source en sections. Guardar crée une cellule ; Atualizar modifie toutes les occurrences de la cellule sélectionnée, avec annulation/rétablissement de la banque.",
                "BYTEBEAT — La fórmula calcula muestras directamente a partir de t. Este contador avanza durante la reproducción. Los operadores >>, &, | y ^ relacionan patrones a distintas escalas temporales. La reproducción directa usa 8 bits.\n\nFÓRMULA COMO PARTITURA — Fórmula → notas interpreta valores como alturas, duraciones, acentos y pausas. Escala y Tom eligen el campo de alturas. Encadear acerca las octavas; Cadenciar resuelve la última nota en la tónica.\n\nBANCO Y CUADRÍCULA — El banco guarda materiales. La cuadrícula determina cuándo y en qué stem suenan. Una célula del banco participa en la canción después de insertarla en la cuadrícula.\n\nIDENTIDAD Y VARIACIÓN — La seed hace reproducible la generación. Variar frase desarrolla un origen en secciones. Guardar crea una célula; Atualizar modifica todas las apariciones de la célula seleccionada, con deshacer/rehacer del banco."
            };
            text = utf8(concepts[language]);
        }
        if (topic != 0) {
            static const juce::String gridTips[4] = {
                utf8("ENTRADA ÚNICA — Clique em Digitar texto para abrir o editor neste quadro; clique em Desenhar para riscar no mesmo espaço. O título do quadro mostra o modo atual e alternar não apaga os dados.\n\nGRADE — Stem 1/2/3 define o destino das novas células e gerações. Clique numa fileira vazia para inserir ali; arraste o centro verticalmente para mover um bloco entre stems. Os controles L/C/R à direita fazem o pan de cada stem e ficam salvos na música. −/+ mudam o zoom horizontal; FIT mostra a música toda. Shift+arraste marca o loop. Desfazer ou Ctrl+Z/Cmd+Z reverte a última edição da grade; a célula continua no banco.\n\nSOM AGORA identifica a fonte que está tocando. O indicador sob a fórmula mostra se ela está só no editor, compilada, no banco ou inserida na grade."),
                "ONE INPUT AREA — Click Digitar texto to open the editor in this box; click Desenhar to draw in the same space. The box title shows the current mode, and switching does not erase either input.\n\nGRID — Stem 1/2/3 chooses the destination for new cells and generated material. Click an empty lane to insert there; drag a block vertically to move it between stems. The L/C/R controls on the right pan each stem and are saved with the song. −/+ change horizontal zoom; FIT shows the whole song. Shift+drag marks a loop. Desfazer or Ctrl+Z/Cmd+Z reverses the last grid edit; the cell stays in the bank.\n\nSOM AGORA identifies the source currently playing. The formula indicator shows whether the expression is only in the editor, compiled, in the bank, or placed in the grid.",
                "UNE SEULE ZONE — Cliquez sur Digitar texto pour ouvrir le champ dans ce cadre ; cliquez sur Desenhar pour tracer dans le même espace. Le titre du cadre indique le mode actif et le changement de mode n’efface aucune entrée.\n\nGRILLE — Stem 1/2/3 choisit la destination des nouvelles cellules et générations. Cliquez sur une ligne vide pour insérer ; faites glisser un bloc verticalement pour changer de stem. Les commandes L/C/R à droite règlent le panoramique et sont enregistrées avec la musique. −/+ changent le zoom horizontal ; FIT affiche toute la musique. Maj+glisser marque la boucle. Desfazer ou Ctrl+Z/Cmd+Z annule la dernière édition de la grille ; la cellule reste dans la banque.\n\nSOM AGORA indique la source actuellement jouée. L’indicateur de formule précise si l’expression est dans l’éditeur, compilée, dans la banque ou insérée dans la grille.",
                "UNA SOLA ZONA — Pulse Digitar texto para abrir el editor en este cuadro; pulse Desenhar para dibujar en el mismo espacio. El título del cuadro indica el modo actual y cambiar de modo no borra ninguna entrada.\n\nCUADRÍCULA — Stem 1/2/3 elige el destino de las nuevas células y generaciones. Haga clic en una fila vacía para insertar; arrastre un bloque verticalmente para cambiar de stem. Los controles L/C/R a la derecha ajustan el panorama y se guardan con la música. −/+ cambian el zoom horizontal; FIT muestra toda la música. Mayús+arrastrar marca el loop. Desfazer o Ctrl+Z/Cmd+Z deshace la última edición de la cuadrícula; la célula permanece en el banco.\n\nSOM AGORA indica la fuente que está sonando. El indicador de fórmula muestra si la expresión está solo en el editor, compilada, en el banco o insertada en la cuadrícula."
            };
            text += "\n\n" + gridTips[std::clamp(language, 0, 3)];
            static const juce::String mixerTips[4] = {
                utf8("MIXER — Cada stem tem controles próprios de volume, pan, mute (M) e solo (S), à direita da grade. O MASTER controla a saída geral e é salvo com a música; mute e solo valem durante a sessão."),
                "MIXER — Each stem has its own volume, pan, mute (M) and solo (S) controls to the right of the grid. MASTER controls the overall output and is saved with the song; mute and solo apply to the current session.",
                "MIXER — Chaque stem possède ses propres commandes de volume, panoramique, mute (M) et solo (S), à droite de la grille. MASTER règle la sortie générale et est enregistré avec le morceau ; mute et solo s’appliquent à la session.",
                "MIXER — Cada stem tiene controles propios de volumen, panorámica, mute (M) y solo (S), a la derecha de la cuadrícula. MASTER controla la salida general y se guarda con la canción; mute y solo se aplican a la sesión."
            };
            text += "\n\n" + mixerTips[std::clamp(language, 0, 3)];
            static const juce::String roleTips[4] = {
                utf8("PAPEL MUSICAL — Selecione uma célula no banco e marque P: livre, pulso, baixo, harmonia, melodia ou textura. O papel descreve sua intenção; não altera o som. Cada pista tem um papel próprio, escolhido no início da fileira. Em Stem, Sugerir posição considera espaço, papel da célula e papel da pista; Equilibrar som também usa densidade, atividade e RMS estimado. A sugestão só preenche tempo e stem: revise e clique Inserir célula para confirmar."),
                utf8("MUSICAL ROLE — Select a bank cell and choose P: free, pulse, bass, harmony, melody or texture. This describes your intent and does not change the sound. Each lane has its own role selector at the left edge. Sugerir posição considers space and lane/cell role; Equilibrar som also considers event density, activity and estimated RMS. Suggestions only fill time and lane; review and confirm with Inserir célula."),
                utf8("RÔLE MUSICAL — Sélectionnez une cellule et choisissez P : libre, pulsation, basse, harmonie, mélodie ou texture. Cela décrit votre intention sans changer le son. Chaque piste possède son sélecteur de rôle à gauche. Sugerir posição tient compte de l’espace et des rôles de la cellule et de la piste ; Equilibrar som considère aussi densité, activité et RMS estimé. La suggestion remplit seulement le temps et la piste ; vérifiez puis confirmez avec Inserir célula."),
                utf8("PAPEL MUSICAL — Seleccione una célula y elija P: libre, pulso, bajo, armonía, melodía o textura. Esto expresa su intención sin cambiar el sonido. Cada pista tiene su propio selector de papel a la izquierda. Sugerir posição considera el espacio y los papeles de célula y pista; Equilibrar som también usa densidad, actividad y RMS estimado. La sugerencia solo completa el tiempo y la pista; revise y confirme con Inserir célula.")
            };
            text += "\n\n" + roleTips[std::clamp(language, 0, 3)];
            static const juce::String formulaTips[4] = {
                utf8("FÓRMULA → NOTAS — A cada semicolcheia, 0 faz pausa; bits 0–3 escolhem grau, bits 4–5 duração (1–4 passos) e bits 6–7 acento. Escolha escala e movimento e ouça a prévia. Guardar frase cria uma célula com fórmula, eventos e escolhas, e a insere no fim da grade, no stem selecionado. Ao selecioná-la, fórmula e controles são restaurados; salvar a música preserva a frase."),
                "FORMULA → NOTES — At each sixteenth note, 0 makes a rest; bits 0–3 choose scale degree, bits 4–5 set duration (1–4 steps), and bits 6–7 set accent. Choose a scale and motion, then preview. Guardar frase saves the formula, events and choices in a cell at the end of the selected stem. Selecting it restores the formula and controls; saving the song preserves the phrase.",
                "FORMULE → NOTES — À chaque double croche, 0 crée un silence ; les bits 0–3 choisissent le degré, les bits 4–5 la durée (1–4 pas) et les bits 6–7 l’accent. Choisissez gamme et mouvement, puis écoutez l’aperçu. Guardar frase conserve formule, événements et choix dans une cellule à la fin du stem sélectionné. La sélection restaure formule et contrôles ; l’enregistrement conserve la phrase.",
                "FÓRMULA → NOTAS — En cada semicorchea, 0 crea un silencio; los bits 0–3 eligen el grado, los bits 4–5 la duración (1–4 pasos) y los bits 6–7 el acento. Elija escala y movimiento, y escuche la vista previa. Guardar frase conserva fórmula, eventos y opciones en una célula al final del stem seleccionado. Al seleccionarla, restaura fórmula y controles; guardar la música conserva la frase."
            };
            text += "\n\n" + formulaTips[std::clamp(language, 0, 3)];
            static const juce::String scaleTips[4] = {
                utf8("ESCALAS — A lista reúne presets básicos, jazz, klezmer e outras cores: bebop, dominante alterada, diminutas, lídio dominante, Freygish/Ahava Rabbah, Misheberakh, tons inteiros e outros. Com Transfigurar, a fórmula interpola para outra escala do mesmo grupo a cada compasso. Cada preset fornece alturas temperadas; não cria sozinho fraseado, ritmo, cadências ou ornamentação de um gênero. Ouça a prévia junto ao baixo e aos acordes."),
                "SCALES — The list groups basic presets, jazz, klezmer and other colours: bebop, altered dominant, diminished, Lydian dominant, Freygish/Ahava Rabbah, Misheberakh, whole tone and more. With Transfigurar, the formula interpolates toward another scale in the same group each bar. A preset supplies equal-tempered pitches; it does not by itself create a genre's phrasing, rhythm, cadences or ornamentation. Preview it with the bass and chords.",
                "GAMMES — La liste regroupe des presets de base, jazz, klezmer et autres couleurs : bebop, dominante altérée, diminuées, lydien dominant, Freygish/Ahava Rabbah, Misheberakh, tons entiers et autres. Avec Transfigurar, la formule interpole vers une autre gamme du même groupe à chaque mesure. Un preset fournit des hauteurs tempérées ; il ne crée pas à lui seul le phrasé, le rythme, les cadences ou les ornements d’un genre. Écoutez l’aperçu avec la basse et les accords.",
                "ESCALAS — La lista agrupa opciones básicas, jazz, klezmer y otros colores: bebop, dominante alterada, disminuidas, lidia dominante, Freygish/Ahava Rabbah, Misheberakh, tonos enteros y más. Con Transfigurar, la fórmula interpola hacia otra escala del mismo grupo en cada compás. Cada preset ofrece alturas temperadas; por sí solo no crea el fraseo, ritmo, cadencias ni ornamentación de un género. Escuche la vista previa con el bajo y los acordes."
            };
            text += "\n\n" + scaleTips[std::clamp(language, 0, 3)];
            static const juce::String tonicTips[4] = {
                utf8("TÔNICA — O controle Tom escolhe a nota de partida, de Dó a Si. A tônica fica salva com a frase e volta ao selecionar a célula."),
                "TONIC — The Tom control chooses the starting note, from C to B. The tonic is saved with the phrase and restored when its cell is selected.",
                "TONIQUE — Le contrôle Tom choisit la note de départ, de Do à Si. La tonique est enregistrée avec la phrase et restaurée lors de la sélection de sa cellule.",
                "TÓNICA — El control Tom elige la nota inicial, de Do a Si. La tónica se guarda con la frase y se restaura al seleccionar su célula."
            };
            text += "\n\n" + tonicTips[std::clamp(language, 0, 3)];
            static const juce::String formTips[4] = {
                utf8("FORMA DA FRASE — Em Variar frase, V2–V5 oferecem formas clássicas curtas; V6 segue os compassos das seções; V7 acrescenta estado matemático; V8 cria A–A′–ápice–retorno; V9 desenvolve A–B–C e recupera A′. Marque/desmarque seções, edite-as e guarde takes. Mantenha uma seção ativa. Ativar todas restaura a forma sem trocar seed ou variação."),
                "PHRASE FORM — In Variar frase, V2–V5 provide short classic forms; V6 follows section meters; V7 adds mathematical state; V8 builds A–A′–climax–return; V9 develops A–B–C and recovers A′. Toggle and edit sections, then save takes. Keep one section active. Activate all restores the form without changing seed or variation.",
                utf8("FORME DE LA PHRASE — Dans Variar frase, V2–V5 proposent des formes courtes ; V6 suit les mesures des sections ; V7 ajoute un état mathématique ; V8 construit A–A′–sommet–retour ; V9 développe A–B–C puis retrouve A′. Activez et éditez les sections, puis conservez des prises. Gardez une section active. Tout activer restaure la forme sans changer seed ni variation."),
                utf8("FORMA DE LA FRASE — En Variar frase, V2–V5 ofrecen formas breves; V6 sigue los compases de cada sección; V7 añade estado matemático; V8 construye A–A′–clímax–retorno; V9 desarrolla A–B–C y recupera A′. Active y edite secciones y guarde tomas. Mantenga una sección activa. Activar todas restaura la forma sin cambiar semilla ni variación.")
            };
            text += "\n\n" + formTips[std::clamp(language, 0, 3)];
            static const juce::String responseTips[4] = {
                utf8("CHAMADA E RESPOSTA — Escolha direção e intervalo. Uma célula integral procura a primeira seção ativa que comporte resposta: mantém o ritmo, espelha o contorno de alturas e aplica o intervalo escolhido, uma barra depois, a 72% do ganho. Cada bloco gera no máximo uma resposta, sem recursão. Bytebeat bruto e células recortadas não participam; a escolha é salva com a música."),
                "CALL AND RESPONSE — Choose a stem direction and interval. Each full cell looks for its first active section that leaves room for an answer: the rhythm is kept, the pitch contour is mirrored, and the selected interval is applied one bar later at 72% gain. At most one answer is made per block; it cannot recurse. Raw Bytebeat and trimmed cells are excluded. The setting is saved with the song.",
                utf8("APPEL ET RÉPONSE — Choisissez la direction et l’intervalle. Chaque cellule entière cherche sa première section active laissant la place à une réponse : le rythme est conservé, le contour des hauteurs est inversé et l’intervalle choisi est appliqué une mesure plus tard à 72 % du gain. Une seule réponse au maximum par bloc, sans récursion. Bytebeat brut et cellules découpées sont exclus. Le choix est enregistré avec le morceau."),
                utf8("LLAMADA Y RESPUESTA — Elija la dirección y el intervalo. Cada célula completa busca su primera sección activa con espacio para responder: conserva el ritmo, invierte el contorno de alturas y aplica el intervalo elegido un compás después al 72 % de ganancia. Como máximo una respuesta por bloque, sin recursión. Bytebeat bruto y células recortadas quedan excluidos. La opción se guarda con la canción.")
            };
            text += "\n\n" + responseTips[std::clamp(language, 0, 3)];
            static const juce::String responseSelectionTips[4] = {
                utf8("MOTIVO E DENSIDADE DA RESPOSTA — No fim de Resposta, Motivo inteiro conserva todos os ataques; Ataques estruturais retém o primeiro de até quatro regiões da seção; Contorno essencial conserva início, fim e extremos de altura. Depois, Densidade 100/50/25% rarefaz essa seleção de modo estável e mantém o último ataque. Direção, intervalo, espaço, papel e harmonia continuam independentes. A linha de estado informa motivo e densidade. Projeto, recuperação e WAV conservam a escolha."),
                "RESPONSE MOTIF AND DENSITY — At the end of Resposta, Complete motif keeps every attack; Structural attacks retains the first from up to four section regions; Essential contour keeps the beginning, ending and pitch extremes. Density 100/50/25% then thins that selection deterministically and retains the final attack. Direction, interval, space, role and harmony remain independent. Status reports motif and density. Project, recovery and WAV retain the choice.",
                utf8("MOTIF ET DENSITÉ DE RÉPONSE — À la fin de Resposta, Motif entier conserve toutes les attaques ; Attaques structurelles garde la première de quatre régions au plus ; Contour essentiel conserve début, fin et extrêmes de hauteur. Densité 100/50/25 % raréfie ensuite cette sélection de façon stable et garde la dernière attaque. Direction, intervalle, espace, rôle et harmonie restent indépendants. Le statut indique motif/densité. Projet, récupération et WAV conservent le choix."),
                utf8("MOTIVO Y DENSIDAD DE RESPUESTA — Al final de Resposta, Motivo entero conserva todos los ataques; Ataques estructurales guarda el primero de hasta cuatro regiones; Contorno esencial conserva inicio, final y extremos de altura. Densidad 100/50/25 % reduce después la selección de modo estable y conserva el último ataque. Dirección, intervalo, espacio, función y armonía siguen independientes. El estado indica motivo/densidad. Proyecto, recuperación y WAV guardan la opción.")
            };
            text += "\n\n" + responseSelectionTips[std::clamp(language,0,3)];
            static const juce::String responseMemoryTips[4] = {
                utf8("MEMÓRIA E INTENÇÃO — Em Resposta, Independentes conserva o comportamento anterior. Evitar registro repetido desloca uma resposta posterior em uma quarta quando ela repetiria a mesma região; Ligar ao final anterior escolhe a oitava de entrada mais próxima da última nota respondida. Intenção da seção 1–4 pode manter Automática, Ecoar o contorno, criar Contraste de oitava, elevar Tensão, concluir em Resolução ou deixar Silêncio e procurar a seção seguinte. São regras determinísticas, limitadas a uma resposta por bloco; não aprendem nem alteram a célula. Projeto V62 e WAV conservam a decisão."),
                "MEMORY AND INTENTION — In Resposta, Independent keeps the previous behavior. Avoid repeated register shifts a later answer by a fourth when it would repeat the same region; Link to previous ending chooses the entry octave nearest the last answered note. Section intention 1–4 can remain Automatic, Echo the contour, create octave Contrast, raise Tension, end in Resolution, or stay Silent and try the next section. These are deterministic rules, limited to one answer per block; they neither learn nor edit the cell. V62 projects and WAV retain the decision.",
                utf8("MÉMOIRE ET INTENTION — Dans Resposta, Indépendantes garde le comportement antérieur. Éviter le registre répété déplace une réponse ultérieure d’une quarte si elle répète la même région ; Relier à la fin précédente choisit l’octave d’entrée la plus proche de la dernière note répondue. L’intention des sections 1–4 peut être Automatique, Écho, Contraste d’octave, Tension, Résolution ou Silence, qui cherche la section suivante. Règles déterministes, une réponse par bloc, sans apprentissage ni modification de cellule. Projet V62 et WAV conservent le choix."),
                utf8("MEMORIA E INTENCIÓN — En Resposta, Independientes conserva el comportamiento anterior. Evitar registro repetido desplaza una respuesta posterior una cuarta si repite la misma región; Enlazar al final anterior elige la octava de entrada más cercana a la última nota respondida. La intención de las secciones 1–4 puede ser Automática, Eco, Contraste de octava, Tensión, Resolución o Silencio, que busca la sección siguiente. Reglas deterministas, una respuesta por bloque, sin aprendizaje ni edición de la celda. Proyecto V62 y WAV guardan la decisión.")
            };
            text += "\n\n" + responseMemoryTips[std::clamp(language,0,3)];
            static const char* accompanimentTips[] = {
                "ACOMPANHAR — Em Fórmula → notas, Acompanhar mantém a escala e lê a melodia em colcheias, com baixo e duas vozes por compasso ativo. Campos de sete graus usam I–IV–V–I em quatro compassos; outras coleções usam posições análogas de graus, sem pressupor função tonal. Compassos sem notas continuam silenciosos. As vozes pertencem à célula, não a stems separados.",
                "ACCOMPANIMENT — In Fórmula → notas, Acompanhar keeps the scale and reads the melody in eighth notes, adding bass and two voices per active bar. Seven-degree fields use I–IV–V–I over four bars; other collections use analogous degree positions without assuming tonal function. Silent bars remain silent. These voices belong to the cell, not separate stems.",
                "ACCOMPAGNEMENT — Dans Fórmula → notas, Acompanhar conserve la gamme et lit la mélodie en croches, avec basse et deux voix par mesure active. Les champs de sept degrés utilisent I–IV–V–I sur quatre mesures ; les autres collections utilisent des positions analogues sans supposer de fonction tonale. Les mesures silencieuses le restent. Ces voix appartiennent à la cellule, pas à des stems distincts.",
                "ACOMPAÑAMIENTO — En Fórmula → notas, Acompanhar conserva la escala y lee la melodía en corcheas, con bajo y dos voces por compás activo. Los campos de siete grados usan I–IV–V–I en cuatro compases; las otras colecciones usan posiciones análogas sin presuponer función tonal. Los compases silenciosos siguen así. Las voces pertenecen a la célula, no a stems separados."
            };
            text += "\n\n" + utf8(accompanimentTips[language]);
            static const char* chordGuideTips[] = {
                "GUIAR ACORDE — Usa o acompanhamento e aproxima a altura da melodia a uma nota do acorde no início do compasso e dos grupos métricos secundários. Entre apoios, conserva os graus da fórmula com condução por oitavas. Ritmo, pausas, gate e intensidade não mudam. Compare Acompanhar e Guiar acorde com a mesma fórmula, escala, Tom e BPM.",
                "CHORD GUIDE — Uses accompaniment and moves the melody to a nearby chord tone at the start of each bar and secondary metric group. Between anchors it keeps formula degrees with octave voice leading. Rhythm, rests, gates and intensity stay unchanged. Compare Acompanhar and Guiar acorde with the same formula, scale, Tom and BPM.",
                "GUIDAGE D’ACCORD — Utilise l’accompagnement et rapproche la mélodie d’une note de l’accord au début des mesures et groupes métriques secondaires. Entre appuis, les degrés de la formule sont conservés avec conduite par octaves. Rythme, silences, durées et intensité restent identiques. Comparez Acompanhar et Guiar acorde avec formule, gamme, Tom et BPM identiques.",
                "GUÍA DE ACORDE — Usa el acompañamiento y acerca la melodía a una nota del acorde al inicio del compás y de los grupos métricos secundarios. Entre apoyos conserva los grados de la fórmula con conducción por octavas. Ritmo, pausas, duraciones e intensidad no cambian. Compare Acompanhar y Guiar acorde con la misma fórmula, escala, Tom y BPM."
            };
            text += "\n\n" + utf8(chordGuideTips[language]);
            static const char* harmonicPlanTips[] = {
                "PLANO HARMÔNICO — Em Acompanhar ou Guiar acorde, clique em Fórmula → notas para escolher progressão e um/dois compassos por acorde. Retorno usa I–IV–V–I, Ciclo usa I–vi–IV–V e Pedal mantém a tônica; outras coleções usam graus análogos. Com dois compassos por acorde, a frase pode terminar antes do padrão completo. Compare com o mesmo Tom, BPM e nível. Guardar frase conserva as escolhas.",
                "HARMONIC PLAN — In Acompanhar or Guiar acorde, click Fórmula → notas to choose a progression and one/two bars per chord. Retorno uses I–IV–V–I, Ciclo uses I–vi–IV–V and Pedal holds the tonic; other collections use analogous degrees. With two bars per chord the phrase may end before the full pattern. Compare at the same Tom, BPM and level. Guardar frase stores the choices.",
                "PLAN HARMONIQUE — En mode Acompanhar ou Guiar acorde, cliquez sur Fórmula → notas pour choisir progression et une/deux mesures par accord. Retorno utilise I–IV–V–I, Ciclo I–vi–IV–V et Pedal garde la tonique ; les autres collections utilisent des degrés analogues. À deux mesures par accord, la phrase peut finir avant le motif complet. Comparez avec les mêmes Tom, BPM et niveau. Guardar frase conserve les choix.",
                "PLAN ARMÓNICO — En Acompanhar o Guiar acorde, pulse Fórmula → notas para elegir progresión y uno/dos compases por acorde. Retorno usa I–IV–V–I, Ciclo usa I–vi–IV–V y Pedal mantiene la tónica; otras colecciones usan grados análogos. Con dos compases por acorde la frase puede terminar antes del patrón completo. Compare con el mismo Tom, BPM y nivel. Guardar frase conserva las opciones."
            };
            text += "\n\n" + utf8(harmonicPlanTips[language]);
            static const char* swingTips[] = {
                "SWING — Em Fórmula → notas escolha Reto (50:50), Leve (60:40) ou Marcado (70:30). O segundo ataque de cada par é atrasado; os valores da fórmula e os níveis não mudam. Acompanhar/Guiar acorde usam pares de colcheias; os outros movimentos usam semicolcheias. O fim da frase limita a duração. Compare com o mesmo BPM.",
                "SWING — In Fórmula → notas choose Reto (50:50), Leve (60:40) or Marcado (70:30). The second attack in each pair is delayed; formula values and levels remain unchanged. Acompanhar/Guiar acorde use eighth-note pairs; other motions use sixteenths. Phrase end limits duration. Compare at the same BPM.",
                "SWING — Dans Fórmula → notas, choisissez Reto (50:50), Leve (60:40) ou Marcado (70:30). La seconde attaque de chaque paire est retardée ; valeurs de formule et niveaux restent identiques. Acompanhar/Guiar acorde utilisent les croches, les autres mouvements les doubles croches. La fin de phrase limite la durée. Comparez au même BPM.",
                "SWING — En Fórmula → notas elija Reto (50:50), Leve (60:40) o Marcado (70:30). El segundo ataque de cada par se retrasa; valores de fórmula y niveles no cambian. Acompanhar/Guiar acorde usan corcheas; los otros movimientos semicorcheas. El final de frase limita la duración. Compare al mismo BPM."
            };
            text += "\n\n" + utf8(swingTips[language]);
            const char* syncTips[] = {
                "SÍNCOPE — Em Fórmula → notas, Síncope antecipa os apoios internos da melodia em meia posição da subdivisão. O início de cada compasso e o acompanhamento ficam no lugar. Em 6/8, antecipa o segundo grupo. Clique novamente para voltar ao original; Guardar frase conserva a escolha.",
                "SYNCOPATION — In Fórmula → notas, Síncope anticipates internal melodic beat attacks by half a grid position. Bar starts and accompaniment stay in place. In 6/8 it anticipates the second group. Toggle again to restore the original; Guardar frase saves the choice.",
                "SYNCOPE — Dans Fórmula → notas, Síncope avance les appuis mélodiques internes d’un demi-pas de subdivision. Débuts de mesure et accompagnement restent en place. En 6/8, le second groupe est anticipé. Désactivez pour revenir à l’original ; Guardar frase conserve le choix.",
                "SÍNCOPA — En Fórmula → notas, Síncope anticipa apoyos melódicos internos medio paso de subdivisión. Inicios de compás y acompañamiento permanecen. En 6/8 anticipa el segundo grupo. Desactive para volver al original; Guardar frase conserva la elección."
            };
            text += "\n\n" + utf8(syncTips[language]);
            const char* polyTips[] = {
                "POLIRRITMIA — Em Fórmula → notas, escolha 2, 3, 5 ou 7 pulsos por compasso. Guarde células em stems diferentes e alinhe seus inícios na grade: 3 contra 2, por exemplo. Cada célula conserva seu pulso e compartilha o compasso. Swing/síncope ficam suspensos; a opção Usar subdivisão restaura suas escolhas. Valores zero da fórmula ainda produzem pausas.",
                "POLYRHYTHM — In Fórmula → notas choose 2, 3, 5 or 7 pulses per bar. Save cells on different stems and align their starts: for example, 3 against 2. Each cell retains its pulse within the shared bar. Swing/syncopation are suspended; Usar subdivisão restores those choices. Zero formula values still produce rests.",
                "POLYRYTHMIE — Dans Fórmula → notas, choisissez 2, 3, 5 ou 7 pulsations par mesure. Gardez les cellules sur des stems différents et alignez leurs débuts : par exemple 3 contre 2. Chaque cellule conserve son rythme dans la mesure commune. Swing/syncope sont suspendus ; Usar subdivisão restaure ces choix. Les valeurs zéro créent toujours des silences.",
                "POLIRRITMIA — En Fórmula → notas elija 2, 3, 5 o 7 pulsos por compás. Guarde células en stems distintos y alinee sus inicios: por ejemplo 3 contra 2. Cada célula conserva su pulso en el compás compartido. Swing/síncopa se suspenden; Usar subdivisão restaura esas opciones. Los valores cero siguen produciendo pausas."
            };
            text += "\n\n" + utf8(polyTips[language]);
            const char* nativeTips[] = {
                "FRASE V6 — Em Variar frase, escolha V6 · frase nativa do compasso. Os ataques seguem colcheias reais; 6/8 usa grupos 3+3, 5/4 alterna 3+2/2+3 conforme a seed, 7/4 usa 2+2+3, 3+2+2 ou 2+3+2. A–B–A–C mantém reconhecimento e resposta. Guarde como nova célula; V2–V5 preservam seu comportamento anterior.",
                "V6 PHRASE — In Variar frase choose V6 · frase nativa do compasso. Attacks follow actual eighth notes: 6/8 uses 3+3, 5/4 uses 3+2 or 2+3 from the seed, and 7/4 uses 2+2+3, 3+2+2 or 2+3+2. A–B–A–C preserves recognition and response. Save a new cell; V2–V5 retain their earlier behavior.",
                "PHRASE V6 — Dans Variar frase, choisissez V6 · frase nativa do compasso. Les attaques suivent de vraies croches : 6/8 en 3+3, 5/4 en 3+2 ou 2+3 selon la seed, 7/4 en 2+2+3, 3+2+2 ou 2+3+2. A–B–A–C garde reconnaissance et réponse. Créez une nouvelle cellule ; V2–V5 restent inchangées.",
                "FRASE V6 — En Variar frase elija V6 · frase nativa do compasso. Ataques en corcheas reales: 6/8 en 3+3, 5/4 en 3+2 o 2+3 según seed y 7/4 en 2+2+3, 3+2+2 o 2+3+2. A–B–A–C mantiene reconocimiento y respuesta. Guarde otra célula; V2–V5 conservan su comportamiento anterior."
            };
            text += "\n\n" + utf8(nativeTips[language]);
            const char* meterTips[] = {
                "FORMA E POLIMETRIA — Em V6–V9, Variar frase → Editar seção escolhe o compasso e 1–4 compassos de duração. O motivo é estendido no tempo; ×2 ocupa dois compassos. No seletor 4/4, abra Stem 1–5 para dar a cada canal um ciclo 2/4–7/4 ou 6/8. Traços âmbar aparecem naquele canal; as posições continuam em semínimas comuns.",
                "FORM AND POLYMETER — In V6–V9, Variar frase → Editar seção sets meter and a 1–4 bar duration. The motif stretches; ×2 occupies two bars. In the 4/4 selector, open Stem 1–5 to assign each channel a 2/4–7/4 or 6/8 cycle. Amber marks appear in that channel; positions remain common quarter-note units.",
                "FORME ET POLYMÉTRIE — En V6–V9, Variar frase → Editar seção règle la mesure et une durée de 1–4 mesures. Le motif s’étire ; ×2 occupe deux mesures. Dans le sélecteur 4/4, ouvrez Stem 1–5 pour attribuer un cycle 2/4–7/4 ou 6/8. Les repères ambre apparaissent sur ce canal ; les positions restent en noires communes.",
                "FORMA Y POLIMETRÍA — En V6–V9, Variar frase → Editar seção elige compás y una duración de 1–4 compases. El motivo se estira; ×2 ocupa dos compases. En el selector 4/4, abra Stem 1–5 para asignar un ciclo 2/4–7/4 o 6/8. Las marcas ámbar aparecen en ese canal; las posiciones siguen en negras comunes."
            };
            text += "\n\n" + utf8(meterTips[language]);
            const char* responseMeterTips[] = {
                "RESPOSTA EM V6 — A resposta começa na próxima seção real da célula, inclusive com compassos mistos. O ritmo da chamada é ajustado proporcionalmente à duração dessa seção. Se a pista de destino estiver ocupada e a política exigir espaço, procura outra seção ativa; no máximo uma resposta por bloco. Desligar remove a camada sem alterar a célula.",
                "V6 RESPONSE — The answer starts at the cell’s next actual section, including mixed meters. The call rhythm is scaled proportionally to that section’s duration. If the destination is occupied and the policy requires free space, another active section is tried; at most one answer per block. Switching off removes the layer without editing the cell.",
                "RÉPONSE V6 — La réponse commence à la section réelle suivante, y compris en mesures mixtes. Le rythme est ajusté proportionnellement à sa durée. Si la piste cible est occupée et la politique exige de l’espace, une autre section active est essayée ; une réponse au maximum par bloc. Désactiver retire la couche sans modifier la cellule.",
                "RESPUESTA V6 — La respuesta comienza en la siguiente sección real, incluso con compases mixtos. El ritmo se ajusta proporcionalmente a su duración. Si el destino está ocupado y la política exige espacio, prueba otra sección activa; como máximo una respuesta por bloque. Desactivar retira la capa sin editar la célula."
            };
            text += "\n\n" + utf8(responseMeterTips[language]);
            const char* contextTips[] = {
                "HARMONIA CONTEXTUAL — Em Resposta, escolha Harmonia + papel + espaço. A resposta busca notas de baixo/harmonia já soando em outros stems no instante de sua entrada e aproxima suas alturas a elas, por oitavas. Favorece células declaradas Harmonia, depois pistas de harmonia. Sem referência, conserva a resposta por papel/registro. A linha de estado mostra o ID usado. Não analisa áudio nem reconhece acordes por estilo.",
                "HARMONIC CONTEXT — In Resposta choose Harmonia + papel + espaço. The answer finds explicit bass/harmony notes sounding in other stems at its entrance and moves pitches to their nearest octaves. Harmony cells are preferred, then harmony lanes. Without a reference it keeps the role/register answer. Status shows the reference ID. No audio analysis or style-based chord recognition.",
                "CONTEXTE HARMONIQUE — Dans Resposta, choisissez Harmonia + papel + espaço. La réponse utilise les notes explicites de basse/harmonie sonnant ailleurs à son entrée et rapproche ses hauteurs par octaves. Priorité aux cellules Harmonia puis aux pistes harmoniques. Sans référence, réponse par rôle/registre. Le statut indique l’ID utilisé. Sans analyse audio ni reconnaissance stylistique.",
                "CONTEXTO ARMÓNICO — En Resposta elija Harmonia + papel + espaço. La respuesta busca notas explícitas de bajo/armonía sonando en otros stems al entrar y aproxima sus alturas por octavas. Prioriza células Harmonia, después pistas armónicas. Sin referencia conserva función/registro. El estado muestra el ID usado. Sin análisis de audio ni reconocimiento de estilo."
            };
            text += "\n\n" + utf8(contextTips[language]);
            const char* movingTips[] = {
                "TRANSIÇÕES HARMÔNICAS — Resposta → Seguir mudanças harmônicas consulta o contexto a cada novo ataque, acompanhando acordes que mudam durante a resposta. Notas já sustentadas não mudam de altura. Em lacunas sem referência, conserva a nota da resposta por papel/registro. O modo Harmonia + papel + espaço continua fixando o acorde na entrada.",
                "HARMONIC TRANSITIONS — Resposta → Seguir mudanças harmônicas checks context at each new attack, following chord changes during the answer. Sustained notes keep their pitch. Gaps without a reference retain the role/register answer note. Harmonia + papel + espaço still fixes harmony at the entrance.",
                "TRANSITIONS HARMONIQUES — Resposta → Seguir mudanças harmônicas consulte le contexte à chaque nouvelle attaque pour suivre les accords pendant la réponse. Les notes tenues gardent leur hauteur. Sans référence, la note par rôle/registre est conservée. Harmonia + papel + espaço garde l’accord d’entrée.",
                "TRANSICIONES ARMÓNICAS — Resposta → Seguir mudanças harmônicas consulta el contexto en cada nuevo ataque y sigue los cambios de acorde durante la respuesta. Las notas sostenidas conservan su altura. Sin referencia conserva la nota por función/registro. Harmonia + papel + espaço sigue fijando el acorde al entrar."
            };
            text += "\n\n" + utf8(movingTips[language]);
            const char* intentTips[] = {
                "INTENÇÃO DA RESPOSTA — Tensionar início usa uma nota cromática vizinha, fora do acorde, nos ataques curtos que cabem na primeira metade; ataques posteriores voltam ao apoio. Notas longas ficam apoiadas. Resolver último ataque deixa a resposta por papel/registro intacta até o último ataque, que se aproxima da harmonia daquele instante. Sem referência, conserva o comportamento anterior. São gestos explícitos, não análise de função tonal.",
                "RESPONSE INTENT — Tensionar início uses a chromatic neighbour outside the chord for short attacks fitting in the first half; later attacks return to support. Long notes stay supported. Resolver último ataque keeps the role/register response until the last attack, which approaches the current harmony. Without a reference the earlier behavior remains. These are explicit gestures, not tonal-function analysis.",
                "INTENTION — Tensionar início utilise une voisine chromatique hors accord pour les attaques courtes contenues dans la première moitié ; les suivantes reviennent à l’appui. Les notes longues restent appuyées. Resolver último ataque conserve rôle/registre jusqu’à la dernière attaque, rapprochée de l’harmonie actuelle. Sans référence, comportement antérieur. Gestes explicites, sans analyse fonctionnelle.",
                "INTENCIÓN — Tensionar início usa una vecina cromática fuera del acorde en ataques cortos contenidos en la primera mitad; los posteriores vuelven al apoyo. Notas largas apoyadas. Resolver último ataque conserva función/registro hasta el último ataque, aproximado a la armonía actual. Sin referencia conserva lo anterior. Son gestos explícitos, no análisis funcional."
            };
            text += "\n\n" + utf8(intentTips[language]);
            const char* stateTips[] = {
                "ESTADO MATEMÁTICO — Em Variar frase, V7 escolhe por variação um mapa limitado: logístico, tenda, triângulo, serra ou Cantor. O estado inteiro entre colcheias pode controlar grau, pausa, duração, intensidade, registro e duty. Seed e variação reiniciam o percurso; Repetir variação atual recomeça sem criar célula. Máscaras silenciam seções sem mudar o estado das seguintes. Até 56 iterações e 64 notas por frase. É controle de eventos, não feedback de áudio; precisão finita pode produzir ciclos.",
                "MATHEMATICAL STATE — In Variar frase, V7 selects a bounded map by variation: logistic, tent, triangle, saw or Cantor. The integer state between eighth notes can control degree, rest, duration, gain, register and duty. Seed and variation restart the path; Repetir variação atual replays without creating a cell. Masks do not change later state. At most 56 iterations and 64 notes per phrase. Event control, not audio feedback; finite precision may cycle.",
                "ÉTAT MATHÉMATIQUE — Dans V7, la variation choisit une carte bornée : logistique, tente, triangle, dent de scie ou Cantor. L’état entier entre les croches peut contrôler degré, silence, durée, gain, registre et duty. Seed et variation réinitialisent le parcours ; Repetir variação atual rejoue sans nouvelle cellule. Les masques ne changent pas l’état ultérieur. Au plus 56 itérations et 64 notes. Contrôle d’événements, pas feedback audio ; précision finie et cycles possibles.",
                "ESTADO MATEMÁTICO — En V7, la variación elige un mapa limitado: logístico, tienda, triángulo, sierra o Cantor. El estado entero entre corcheas puede controlar grado, pausa, duración, ganancia, registro y duty. Seed y variación reinician el recorrido; Repetir variação atual repite sin crear célula. Las máscaras no alteran el estado posterior. Máximo 56 iteraciones y 64 notas. Control de eventos, no feedback de audio; precisión finita y ciclos posibles."
            };
            text += "\n\n" + utf8(stateTips[language]);
            const char* formFamilyTips[] = {
                "NOVAS FORMAS — Em Variar frase, V8 constrói A–A′–ápice–retorno: o mesmo ritmo reaparece, cresce, alcança o terceiro compasso mais intenso/agudo e volta com cadência. V9 faz A–B–C–A′: dois desenvolvimentos diferentes antes do retorno reconhecível. Ambas seguem os compassos próprios de cada seção, seed, variação, máscaras, edições e takes. A forma organiza eventos; não analisa estilo nem escolhe por você.",
                "NEW FORMS — In Variar frase, V8 builds A–A′–climax–return: the rhythm recurs, grows, reaches a stronger/higher third section, then returns with a cadence. V9 is A–B–C–A′: two distinct developments before a recognizable return. Both follow each section's meter, seed, variation, masks, edits and takes. Form organizes events; it does not analyze style or choose for you.",
                "NOUVELLES FORMES — Dans Variar frase, V8 construit A–A′–sommet–retour : le rythme revient, croît, atteint une troisième section plus intense/aiguë, puis conclut. V9 suit A–B–C–A′ : deux développements distincts avant un retour reconnaissable. Les deux suivent mesures par section, seed, variation, masques, éditions et prises. La forme organise les événements, sans analyser un style ni choisir à votre place.",
                "NUEVAS FORMAS — En Variar frase, V8 construye A–A′–clímax–retorno: el ritmo reaparece, crece, alcanza una tercera sección más intensa/aguda y vuelve con cadencia. V9 usa A–B–C–A′: dos desarrollos distintos antes de un retorno reconocible. Ambas siguen compases por sección, semilla, variación, máscaras, ediciones y tomas. La forma organiza eventos; no analiza estilos ni decide por usted."
            };
            text += "\n\n" + utf8(formFamilyTips[language]);
            const char* delayTips[] = {
                "DELAY POR STEM — Em FX escolha colcheia, pontuada ou semínima, seguindo o BPM. Mistura controla quanto eco se ouve (0–60%, passos de 10%); realimentação controla as repetições (0–60%, passos de 5%). Comece em 30%/35%; Restaurar volta a esses valores. Compare 0% e 40% de realimentação na mesma célula: zero deixa uma repetição. Mistura zero mantém o som seco. Ajustes são suavizados e salvos. Um preset por pista, sem empilhar filtro/bitcrush. WAV: até 10 min de grade + cauda comum aos stems, no máximo 21,1 s.",
                "STEM DELAY — In FX choose eighth, dotted eighth or quarter, following BPM. Mix controls audible echo (0–60%, 10% steps); feedback controls repeats (0–60%, 5% steps). Start at 30%/35%; Restore returns there. Compare 0% and 40% feedback on the same cell: zero leaves one echo. Zero mix keeps dry sound. Changes are smoothed and saved. One preset per lane, without filter/bitcrush stacking. WAV: up to 10 min of grid plus a shared stem tail, at most 21.1 s.",
                "DELAY PAR STEM — Dans FX choisissez croche, croche pointée ou noire, suivant le BPM. Mélange : écho audible (0–60 %, pas de 10 %) ; réinjection : répétitions (0–60 %, pas de 5 %). Commencez à 30 %/35 % ; Restaurer rétablit ces valeurs. Comparez 0 % et 40 % de réinjection : zéro laisse un écho. Mélange nul : son sec. Réglages lissés et sauvegardés. Un preset par piste, sans empilage filtre/bitcrush. WAV : 10 min de grille au plus + traîne commune, au maximum 21,1 s.",
                "DELAY POR STEM — En FX elija corchea, corchea con puntillo o negra, siguiendo BPM. Mezcla: eco audible (0–60 %, pasos de 10 %); realimentación: repeticiones (0–60 %, pasos de 5 %). Empiece en 30 %/35 %; Restaurar vuelve allí. Compare 0 % y 40 % de realimentación: cero deja un eco. Mezcla cero mantiene sonido seco. Ajustes suavizados y guardados. Un preset por pista, sin apilar filtro/bitcrush. WAV: hasta 10 min de cuadrícula + cola común, máximo 21,1 s."
            };
            text += "\n\n" + utf8(delayTips[language]);
            const char* reverbTips[] = {
                "REVERB POR STEM — Em FX escolha curto/longo e Reverb · mistura: 0–60%, passos de 5%; Restaurar volta a 25%. Zero mantém o seco. Reverb · decaimento oferece 0,25–3 s aproximados: ajuste pela mesma célula curta seguida de silêncio. O valor substitui o decaimento curto/longo; Padrão do preset recupera essa diferença. É uma estimativa em graves, não duração exata nem tamanho de sala: o amortecimento encurta os agudos. Mudanças suaves, independentes por stem, salvas em projeto/WAV, pan conservado. Sem empilhar delay/filtro. Cauda exportada: 4,1–6,1 s ou a maior de outro stem. Reiniciar limpa o histórico.",
                "STEM REVERB — In FX choose short/long and Reverb · mistura: 0–60%, 5% steps; Restore returns to 25%. Zero stays dry. Reverb · decaimento offers approximate 0.25–3 s: compare on the same short cell followed by silence. The value overrides short/long decay; Preset default restores their difference. It estimates bass decay, not exact duration or room size: damping shortens treble. Smoothed, independent per stem, saved in project/WAV, pan retained. No delay/filter stacking. Export tail: 4.1–6.1 s or a longer stem tail. Restart clears history.",
                "REVERB PAR STEM — Dans FX choisissez court/long et Reverb · mistura : 0–60 %, pas de 5 % ; Restaurer revient à 25 %. Zéro reste sec. Reverb · decaimento : 0,25–3 s approximatives, à comparer sur une cellule courte suivie de silence. Cette valeur remplace court/long ; Padrão do preset rétablit leur différence. Estimation dans les graves, ni durée exacte ni taille de salle : les aigus décroissent plus vite. Réglages lissés par stem, sauvegardés dans projet/WAV, panoramique conservé. Sans empilage delay/filtre. Traîne exportée : 4,1–6,1 s ou celle plus longue d’un autre stem. Redémarrer efface l’historique.",
                "REVERB POR STEM — En FX elija corto/largo y Reverb · mistura: 0–60 %, pasos de 5 %; Restaurar vuelve a 25 %. Cero mantiene el seco. Reverb · decaimento ofrece 0,25–3 s aproximados: compare la misma célula corta seguida de silencio. El valor sustituye corto/largo; Padrão do preset recupera su diferencia. Estima decaimiento grave, no duración exacta ni tamaño de sala: los agudos decaen más rápido. Ajustes suaves por stem, guardados en proyecto/WAV, panorama conservado. Sin apilar delay/filtro. Cola exportada: 4,1–6,1 s o la mayor de otro stem. Reiniciar borra el historial."
            };
            text += "\n\n" + utf8(reverbTips[language]);
            const char* filterTips[] = {
                "CORTE DO FILTRO — Em FX escolha Quente, Escuro, Grão ou Lo-fi e abra Filtro · corte. Frequências menores retiram mais agudos; maiores preservam mais brilho. Compare 320 Hz e 8000 Hz com a mesma célula e volume. Padrão do preset restaura 5 kHz em Quente/Grão, 1,2 kHz em Escuro e 2,5 kHz em Lo-fi. O corte escolhido vale para esses quatro presets neste stem, sem mudar os bits de Grão/Lo-fi. Delay, reverb e Bypass não usam esse ajuste. Transição suave, salvo no projeto e no WAV; sem ressonância.",
                "FILTER CUTOFF — In FX choose Quente, Escuro, Grão or Lo-fi, then Filtro · corte. Lower frequencies remove more treble; higher values retain brightness. Compare 320 Hz and 8000 Hz on the same cell at the same volume. Preset default restores 5 kHz for Quente/Grão, 1.2 kHz for Escuro and 2.5 kHz for Lo-fi. The chosen cutoff applies to these four presets on this stem without changing bit depth. Delay, reverb and Bypass ignore it. Smoothed, saved in project and WAV; no resonance.",
                "COUPURE DU FILTRE — Dans FX choisissez Quente, Escuro, Grão ou Lo-fi, puis Filtro · corte. Une fréquence basse réduit les aigus ; une valeur haute conserve la brillance. Comparez 320 Hz et 8000 Hz sur la même cellule au même volume. Padrão do preset rétablit 5 kHz (Quente/Grão), 1,2 kHz (Escuro), 2,5 kHz (Lo-fi). Le réglage vaut pour ces quatre presets du stem, sans changer les bits. Delay, reverb et Bypass l’ignorent. Transition lissée, projet/WAV sauvegardés ; sans résonance.",
                "CORTE DEL FILTRO — En FX elija Quente, Escuro, Grão o Lo-fi y abra Filtro · corte. Frecuencias bajas reducen agudos; altas conservan brillo. Compare 320 Hz y 8000 Hz con la misma célula y volumen. Padrão do preset restaura 5 kHz (Quente/Grão), 1,2 kHz (Escuro), 2,5 kHz (Lo-fi). El corte vale para estos cuatro presets del stem sin cambiar bits. Delay, reverb y Bypass lo ignoran. Transición suave, guardado en proyecto y WAV; sin resonancia."
            };
            text += "\n\n" + utf8(filterTips[language]);
            const char* strengthTips[] = {
                "FORÇA DA LEITURA — Em Leitura / glifo escolha Contorno, Ritmo, Intensidade, Tempo invertido ou um Gesto temporal. Acelerar começa espaçado e comprime o final; Desacelerar faz o inverso; Respirar alterna expansão e compressão. Todos preservam início/fim da célula e a ordem dos ataques. Força 0% recupera exatamente o original; 25/50/75% mistura os tempos; 100% aplica o gesto inteiro. Não é BPM nem volume. Compare 0/50/100% na mesma entrada. Guardar cria uma célula; Atualizar confirma com desfazer. Projeto e recuperação guardam modo/força; fórmulas e frases generativas não os usam.",
                "READING STRENGTH — In Leitura / glifo choose Contour, Rhythm, Dynamics, Reverse time or a Temporal gesture. Accelerate starts spaced and compresses the ending; Decelerate does the opposite; Breathe alternates expansion and compression. All retain the cell boundaries and attack order. Strength 0% restores the exact original; 25/50/75% blends timing; 100% applies the full gesture. It is neither BPM nor volume. Compare 0/50/100% on one input. Guardar creates a cell; Atualizar confirms with undo. Project/recovery retain mode and strength; formulas and generated phrases do not use them.",
                "FORCE DE LECTURE — Dans Leitura / glifo choisissez Contour, Rythme, Intensité, Temps inversé ou un Geste temporel. Accélérer commence espacé puis comprime la fin ; Décélérer fait l’inverse ; Respirer alterne expansion et compression. Tous conservent les limites de la cellule et l’ordre des attaques. 0 % retrouve exactement l’original ; 25/50/75 % mélangent le temps ; 100 % applique le geste entier. Ni BPM ni volume. Comparez 0/50/100 %. Guardar crée une cellule ; Atualizar confirme avec annulation. Projet/récupération conservent mode/force ; formules et phrases génératives excluses.",
                "FUERZA DE LECTURA — En Leitura / glifo elija Contorno, Ritmo, Intensidad, Tiempo invertido o un Gesto temporal. Acelerar empieza espaciado y comprime el final; Desacelerar hace lo contrario; Respirar alterna expansión y compresión. Todos conservan límites de la célula y orden de ataques. 0 % recupera exactamente el original; 25/50/75 % mezcla tiempos; 100 % aplica el gesto completo. No es BPM ni volumen. Compare 0/50/100 %. Guardar crea una célula; Atualizar confirma con deshacer. Proyecto/recuperación guardan modo/fuerza; fórmulas y frases generativas no los usan."
            };
            text += "\n\n" + utf8(strengthTips[language]);
            const char* authorialGestureTips[] = {
                "GESTO AUTORAL — Leitura / glifo oferece tempo, intensidade crescente/decrescente e altura crescente/decrescente/em arco. A curva modela ganho ou alturas sem alterar duração, ordem ou horários. Para altura, escolha alcance de 6, 12 ou 24 semitons; o arco retorna à afinação original após o pico. O ponto central modela o percurso e a força mistura com o original.",
                "AUTHORIAL GESTURE — Leitura / glifo offers time, rising/falling intensity and rising/falling pitch. The curve shapes gain or pitch without changing duration, order or timing. For pitch, choose a range of 6, 12 or 24 semitones; the curve shifts each attack relative to its source pitch. Shape the path with the middle point; strength blends with the original.",
                "GESTE AUTEUR — Leitura / glifo propose temps, intensité ascendante/descendante et hauteur ascendante/descendante. La courbe modèle gain ou hauteur sans changer durée, ordre ni horaires. Pour la hauteur, choisissez 6, 12 ou 24 demi-tons ; chaque attaque est transposée depuis sa hauteur source. Le point central modèle le trajet et la force mélange avec l’original.",
                "GESTO AUTORAL — Leitura / glifo ofrece tiempo, intensidad ascendente/descendente y altura ascendente/descendente. La curva modela ganancia o altura sin cambiar duración, orden ni tiempos. Para altura, elija 6, 12 o 24 semitonos; cada ataque se transpone desde su afinación original. El punto central modela el recorrido y la fuerza mezcla con el original."
            };
            text += "\n\n" + utf8(authorialGestureTips[language]);
            const char* libraryTips[] = {
                "BIBLIOTECA MULTIPROJETO — No fim do banco, Indexar projetos externos permite escolher explicitamente até 8 arquivos .risco, 4 MiB cada. A leitura é sequencial; não varre pastas. Buscar no índice consulta nome, projeto, fórmula, autor, origem ou ID e mostra até 128 resultados. Escolha um candidato: Ouvir antes de importar usa BPM/compasso atuais e não altera banco/grade; Importar confirma só a célula no banco. O candidato continua acessível no menu até importar ou limpar. Proveniência, créditos/licença e IDs externos são conservados. Índice e candidato existem só nesta sessão; Limpar índice não apaga arquivos.",
                "MULTI-PROJECT LIBRARY — At the end of the bank, Index external projects explicitly selects up to 8 .risco files, 4 MiB each. Reading is sequential and never scans folders. Search by name, project, formula, author, source or ID, up to 128 results. Choose a candidate: Listen before importing uses current BPM/meter without changing bank/grid; Import confirms that cell only. The candidate remains available until imported or cleared. Provenance, credits/license and external IDs are retained. Index and candidate last only for this session; clearing does not delete files.",
                "BIBLIOTHÈQUE MULTIPROJET — En bas de la banque, Indexer des projets externes choisit explicitement jusqu’à 8 fichiers .risco de 4 MiB chacun. Lecture séquentielle, sans parcours de dossiers. Recherchez nom, projet, formule, auteur, source ou ID, jusqu’à 128 résultats. Choisissez un candidat : écouter avant import emploie BPM/mesure courants sans modifier banque/grille ; importer confirme cette cellule seule. Le candidat reste disponible. Provenance, crédits/licence et IDs externes sont conservés. Index/candidat limités à la session ; vider l’index ne supprime aucun fichier.",
                "BIBLIOTECA MULTIPROYECTO — Al final del banco, Indexar proyectos externos selecciona explícitamente hasta 8 archivos .risco de 4 MiB cada uno. Lectura secuencial, sin recorrer carpetas. Busque nombre, proyecto, fórmula, autor, origen o ID, hasta 128 resultados. Elija candidato: escuchar antes de importar usa BPM/compás actuales sin cambiar banco/cuadrícula; importar confirma solo esa célula. El candidato sigue disponible. Procedencia, créditos/licencia e IDs externos se conservan. Índice/candidato duran solo la sesión; limpiar no borra archivos."
            };
            text += "\n\n" + utf8(libraryTips[language]);
            const char* libraryPersistenceTips[] = {
                "CATÁLOGO PERSISTENTE — Risco/project-library.json guarda hasta 8 caminos e 16 agrupamentos nomeados, sem copiar conteúdo. Um agrupamento reúne até 8 fontes do catálogo; agrupamentos podem compartilhar fontes. Crie pelo menu do banco e busque dentro dele; os resultados vêm somente de fontes já indexadas. Nada é lido ao abrir o app: use Reindexar catálogo salvo. Limpar catálogo remove as referências locais, não os arquivos-fonte.",
                "PERSISTENT CATALOG — Risco/project-library.json stores up to 8 paths and 16 named groups, without copying project contents. A group contains up to 8 catalog sources; groups may share sources. Create one from the bank menu and search within it; results come only from indexed sources. Nothing is read at launch: use Reindexar catálogo salvo. Clearing the catalog removes local references, never source files.",
                "CATALOGUE PERSISTANT — Risco/project-library.json conserve jusqu’à 8 chemins et 16 groupes nommés, sans copier le contenu. Un groupe réunit jusqu’à 8 sources du catalogue ; les sources peuvent être partagées. Créez-le dans le menu de la banque et recherchez en son sein ; seuls les fichiers déjà indexés donnent des résultats. Aucune lecture au lancement : utilisez Reindexar catálogo salvo. Effacer le catalogue retire les références locales, jamais les sources.",
                "CATÁLOGO PERSISTENTE — Risco/project-library.json guarda hasta 8 rutas y 16 grupos con nombre, sin copiar contenido. Cada grupo reúne hasta 8 fuentes del catálogo y pueden compartirlas. Créelo desde el menú del banco y busque en él; los resultados solo incluyen fuentes indexadas. No se lee nada al iniciar: use Reindexar catálogo salvo. Limpiar el catálogo elimina referencias locales, nunca los archivos fuente."
            };
            text += "\n\n" + utf8(libraryPersistenceTips[language]);
            const char* ayNoiseTips[] = {
                "RUÍDO DO ESTUDO AY — No seletor de timbre compare AY · tons, AY · misto (25% ruído) e AY · ruído (só ruído). No mesmo menu, AY · período do ruído oferece 1/2/4/8/16/31; maior período deixa os pulsos mais lentos. É global à música e não muda as alturas do modo tons. Use uma célula curta para comparar com o mesmo volume. Ruído determinístico de 17 bits, redução para domínio de 8 kHz e interpolação próprias; compartilhado entre até três vozes da célula, reiniciando ao repetir a célula. Não é mixer lógico, envelope, DAC ou emulação fiel do chip. Projeto/recuperação/WAV guardam modo e período. Fórmulas diretas não mudam.",
                "AY STUDY NOISE — In the timbre selector compare AY · tons, AY · misto (25% noise) and AY · ruído (noise only). The same menu offers AY noise period 1/2/4/8/16/31; larger periods slow the pulses. Global song setting; tone-only pitch remains unchanged. Compare a short cell at equal volume. Deterministic 17-bit noise with custom 8 kHz-domain reduction and interpolation; shared by up to three voices within a cell, restarting when the cell repeats. Not chip logic mixing, hardware envelope, DAC or faithful emulation. Project/recovery/WAV retain mode and period. Direct formulas stay unchanged.",
                "BRUIT DE L’ÉTUDE AY — Comparez AY · tons, AY · misto (25 % bruit), AY · ruído (bruit seul) dans le sélecteur de timbre. Même menu : période 1/2/4/8/16/31 ; valeur plus grande, impulsions plus lentes. Réglage global, sans changer les hauteurs du mode tons. Comparez une cellule courte au même volume. Bruit déterministe de 17 bits, réduction en domaine 8 kHz et interpolation propres ; partagé par trois voix au plus dans la cellule, redémarré à sa répétition. Ni mixeur logique, enveloppe matérielle, DAC ni émulation fidèle. Projet/récupération/WAV conservent mode/période. Formules directes inchangées.",
                "RUIDO DEL ESTUDIO AY — Compare AY · tons, AY · misto (25 % ruido), AY · ruído (solo ruido) en el selector de timbre. Mismo menú: período 1/2/4/8/16/31; mayor valor ralentiza pulsos. Ajuste global, sin cambiar alturas del modo tons. Compare una célula corta al mismo volumen. Ruido determinista de 17 bits, reducción al dominio 8 kHz e interpolación propias; compartido por hasta tres voces en la célula, reiniciado al repetirla. No reproduce mezcla lógica, envolvente de hardware, DAC ni emulación fiel. Proyecto/recuperación/WAV conservan modo/período. Fórmulas directas sin cambios."
            };
            text += "\n\n" + utf8(ayNoiseTips[language]);
            const char* ayEnvelopeTips[] = {
                "ENVELOPE DO ESTUDO AY — No seletor de timbre abra AY · envelope: Risco original, Cair uma vez, Subir/sustentar, Triângulo ou Cair/repetir. AY · rampa escolhe 0,125–2 s aproximados para 16 degraus; triângulo usa duas rampas por ciclo. Compare uma nota longa com rampa curta e longa, depois Risco original. Cada nota reinicia a curva; a duração da nota e as bordas suaves do Risco continuam limitando o som. Atua em tons/misto/ruído AY, não nas fórmulas diretas ou nos outros motores. São curvas lineares inspiradas, sem envelope global do chip, DAC logarítmico ou todas as formas de registrador. Modo/tempo são globais e salvos em projeto, recuperação e WAV.",
                "AY STUDY ENVELOPE — In the timbre selector open AY · envelope: Risco original, one-shot decay, rise/hold, triangle or repeated decay. AY · rampa selects an approximate 0.125–2 s for 16 steps; triangle uses two ramps per cycle. Compare a long note with short/long ramp, then Risco original. Each note restarts the curve; note duration and Risco edge fades still constrain sound. Applies to AY tones/mixed/noise, not direct formulas or other engines. Inspired linear curves, without a shared chip envelope, logarithmic DAC or all register shapes. Global mode/time are saved in project, recovery and WAV.",
                "ENVELOPPE DE L’ÉTUDE AY — Dans le sélecteur ouvrez AY · envelope : Risco original, chute unique, montée/maintien, triangle, chute répétée. AY · rampa : 0,125–2 s approximatives pour 16 paliers ; triangle : deux rampes par cycle. Comparez une note longue avec rampe courte/longue, puis Risco original. Chaque note redémarre la courbe ; durée de note et fondus Risco restent actifs. Tons/mixte/bruit AY seulement, pas formules directes ni autres moteurs. Courbes linéaires inspirées, sans enveloppe globale du chip, DAC logarithmique ou toutes les formes de registre. Mode/temps globaux sauvegardés dans projet, récupération et WAV.",
                "ENVOLVENTE DEL ESTUDIO AY — En el selector abra AY · envelope: Risco original, caída única, subida/sostener, triángulo o caída repetida. AY · rampa: 0,125–2 s aproximados para 16 escalones; triángulo usa dos rampas por ciclo. Compare una nota larga con rampa corta/larga, después Risco original. Cada nota reinicia la curva; duración y fundidos Risco siguen activos. Solo tonos/mixto/ruido AY, no fórmulas directas ni otros motores. Curvas lineales inspiradas, sin envolvente global del chip, DAC logarítmico o todas las formas de registro. Modo/tiempo globales guardados en proyecto, recuperación y WAV."
            };
            text += "\n\n" + utf8(ayEnvelopeTips[language]);
            const char* historicalAyTips[] = {
                "AY/YM HISTÓRICO — Escolha AY-3-8910 · histórico ou YM2149 · histórico. Cada stem mantém um chip independente com três canais, períodos de 12 bits, volume de 4 bits e curva DAC do modelo. No mesmo seletor, Histórico · mixer escolhe tons, tons + ruído ou ruído; Histórico · ruído define o período compartilhado; Histórico · envelope e rampa controlam a envolvente compartilhada. Um ataque novo reinicia a forma. Esses controles são separados dos menus AY do estudo expandido. Compare AY/YM na mesma grade; fórmulas diretas continuam em caminho separado. Projeto V62 e WAV preservam os ajustes. É um alvo histórico documentado, não certificação absoluta de hardware.",
                "HISTORICAL AY/YM — Choose AY-3-8910 · histórico or YM2149 · histórico. Each stem keeps an independent three-channel chip with 12-bit periods, 4-bit volume and the model DAC curve. In the same selector, Histórico · mixer chooses tone, tone + noise or noise; Histórico · ruído sets the shared noise period; Histórico · envelope and rampa control the shared envelope. A new attack retriggers its shape. These controls are separate from the expanded AY study menus. Compare AY/YM on the same grid; direct formulas remain a separate path. V62 projects and WAV retain the settings. This is a documented historical target, not absolute hardware certification.",
                "AY/YM HISTORIQUE — Choisissez AY-3-8910 · histórico ou YM2149 · histórico. Chaque stem garde une puce indépendante à trois canaux, périodes 12 bits, volume 4 bits et courbe DAC du modèle. Dans le même sélecteur, Histórico · mixer choisit tons, tons + bruit ou bruit ; Histórico · ruído règle la période partagée ; Histórico · envelope et rampa commandent l’enveloppe partagée. Une nouvelle attaque la redémarre. Ces contrôles sont séparés de l’étude AY étendue. Comparez AY/YM sur la même grille ; les formules directes restent une voie séparée. Le projet V60 et le WAV gardent les réglages. Cible historique documentée, sans certification absolue du matériel.",
                "AY/YM HISTÓRICO — Elija AY-3-8910 · histórico o YM2149 · histórico. Cada stem mantiene un chip independiente de tres canales, períodos de 12 bits, volumen de 4 bits y curva DAC del modelo. En el mismo selector, Histórico · mixer elige tonos, tonos + ruido o ruido; Histórico · ruído fija el período compartido; Histórico · envelope y rampa controlan la envolvente compartida. Un ataque nuevo reinicia la forma. Son controles separados del estudio AY expandido. Compare AY/YM en la misma cuadrícula; las fórmulas directas siguen en otra ruta. Proyecto V60 y WAV conservan los ajustes. Objetivo histórico documentado, no certificación absoluta del hardware."
            };
            text += "\n\n" + utf8(historicalAyTips[language]);
            const char* loudnessTips[] = {
                "LUFS E EXPORTAÇÃO LONGA — Na saída, M acompanha 400 ms, S os últimos 3 s e I a média medida desde o reset, descartando blocos muito silenciosos. Não é RMS nem volume automático. Traços indicam janela ainda incompleta; <=-70 indica o piso da leitura. Em Salvar, Reiniciar medição LUFS permite começar uma tomada, sem alterar áudio. Pausar preserva I; abrir projeto reinicia a medição. Pré-escutas também entram no medidor ao vivo. WAV mede só os frames exportados e informa I ao concluir, antes da quantização PCM; não usa o histórico ao vivo. Exporta até 10 min de grade + cauda, por arquivo ou lote; reserva 5 GB livres, usa provisório e permite cancelar, com limite de processamento de 120 s por operação. Janela agora admite 1024×720, com controles proporcionais e fonte sem deformação horizontal.",
                "LUFS AND LONG EXPORT — Output M follows 400 ms, S the last 3 s, and I the average since reset, excluding very quiet blocks. Neither RMS nor automatic gain. Dashes mean an incomplete window; <=-70 is the reading floor. In Salvar, Reiniciar medição LUFS starts a measurement without altering sound. Pause retains I; opening a project resets measurement. Live previews also count. WAV measures exported frames only and reports I on completion, before PCM quantization, independently of live history. Up to 10 min of grid plus tail, single or batch; preserves 5 GB free, uses temporary output and cancellation, with 120 s processing limit per operation. Window now supports 1024×720 with proportional controls and no horizontal font distortion.",
                "LUFS ET EXPORT LONG — M suit 400 ms, S les 3 dernières secondes, I la moyenne depuis le reset en écartant les blocs très faibles. Ni RMS ni gain automatique. Tirets : fenêtre incomplète ; <=-70 : plancher. Dans Salvar, Reiniciar medição LUFS recommence sans modifier le son. Pause conserve I ; ouvrir un projet réinitialise. Préécoutes incluses en direct. WAV mesure seulement les frames exportées et indique I avant quantification PCM, indépendamment du direct. Jusqu’à 10 min de grille + traîne, fichier ou lot ; réserve de 5 GB, provisoire, annulation, limite de traitement 120 s par opération. Fenêtre 1024×720 admise, contrôles proportionnels, police sans déformation horizontale.",
                "LUFS Y EXPORTACIÓN LARGA — M sigue 400 ms, S los últimos 3 s, I la media desde reiniciar, excluyendo bloques muy silenciosos. No es RMS ni ganancia automática. Guiones: ventana incompleta; <=-70: piso. En Salvar, Reiniciar medição LUFS empieza medición sin alterar audio. Pausa conserva I; abrir proyecto reinicia. Preescuchas incluidas en vivo. WAV mide solo frames exportados e informa I antes de cuantización PCM, independiente del historial en vivo. Hasta 10 min de cuadrícula + cola, archivo o lote; reserva 5 GB, provisional, cancelación, límite de procesamiento 120 s por operación. Ventana 1024×720, controles proporcionales y fuente sin deformación horizontal."
            };
            text += "\n\n" + utf8(loudnessTips[language]);
            const char* takeTips[] = {
                "TAKES — Em Variar frase, Guardar prévia como novo take conserva seed, variação, versão, seções, compassos e edições numa célula independente. Ouvir e preferir take desta seed lista frases guardadas com a mesma seed, incluindo o original. Guarde a prévia antes de trocar; a troca descarta edições ainda não guardadas. A preferência volta ao abrir o projeto salvo, sem iniciar áudio; a grade mantém suas células. Para usar o take na composição, insira sua célula na grade. O banco aceita 128 células.",
                "TAKES — In Variar frase, Guardar prévia como novo take stores seed, variation, version, sections, meters and edits as an independent cell. Ouvir e preferir take desta seed lists saved phrases sharing the seed, including the original. Save the preview before switching; unsaved edits are discarded by a switch. The preference returns when opening the saved project, without starting audio; grid cells stay unchanged. Place the chosen cell to use it in the composition. The bank limit is 128 cells.",
                "PRISES — Dans Variar frase, Guardar prévia como novo take conserve graine, variation, version, sections, mesures et éditions dans une cellule indépendante. Ouvir e preferir take desta seed liste les phrases conservées de même graine, dont l’original. Conservez la préécoute avant de changer ; les éditions non conservées sont abandonnées. La grille accepte 128 cellules.",
                "TOMAS — En Variar frase, Guardar prévia como novo take conserva semilla, variación, versión, secciones, compases y ediciones en una célula independiente. Ouvir e preferir take desta seed lista frases guardadas con la misma semilla, incluido el original. Guarde la previa antes de cambiar; al cambiar se descartan ediciones sin guardar. La cuadrícula acepta 128 células."
            };
            text += "\n\n" + utf8(takeTips[language]);
            const char* gapTips[] = {
                "RESPOSTAS NAS PAUSAS: em Resposta, escolha a direção e depois Pausas internas + harmonia. Cada nota deve caber inteira numa pausa do destino; as demais são omitidas. Sem espaço na primeira seção, tenta a próxima. Mantém papel, registro e harmonia por ataque. Fórmulas de áudio bruto ocupam todo o bloco; mute e caudas de efeitos não definem pausas. Ao atingir o limite de análise, omite candidatos restantes.",
                "RESPONSES IN RESTS: in Response, choose a direction, then Pausas internas + harmonia. Each note must fit entirely in a destination rest; other notes are omitted. If the first section is full, the next is tried. Role, register and harmony per attack are retained. Raw audio formulas occupy their entire block; mute and effect tails do not define rests. At the analysis limit, remaining candidates are omitted.",
                "RÉPONSES DANS LES SILENCES : dans Réponse, choisissez la direction puis Pausas internas + harmonia. Chaque note doit tenir entièrement dans un silence de la piste cible ; sinon elle est omise. Si la première section est pleine, la suivante est essayée. Rôle, registre et harmonie par attaque sont conservés. Les formules audio brutes occupent tout le bloc ; mute et queues d’effets ne définissent pas les silences. À la limite d’analyse, les candidats restants sont omis.",
                "RESPUESTAS EN SILENCIOS: en Respuesta, elija la dirección y después Pausas internas + harmonia. Cada nota debe caber entera en un silencio del destino; las demás se omiten. Si la primera sección está llena, prueba la siguiente. Conserva papel, registro y armonía por ataque. Las fórmulas de audio bruto ocupan todo el bloque; mute y colas de efectos no definen silencios. Al alcanzar el límite de análisis se omiten los candidatos restantes."
            };
            text += "\n\n" + utf8(gapTips[language]);
            const char* automationTips[] = {
                "AUTOMAÇÃO POR STEM — No menu FX, Volume · automação usa três níveis em 0%, 50% e 100% da grade. Pan · automação move suavemente entre esquerda, centro e direita nos mesmos pontos. FX · automação escolhe se o preset atual toca na entrada, meio e fim; as trocas ocorrem a 25% e 75%, com transição suave para bypass, e uma cauda pode terminar naturalmente. Filtro, delay, mistura/decaimento do reverb e profundidade do bitcrush têm curvas internas de três pontos; no bitcrush, os degraus são inteiros de 2–16 bits. Editar ativa a curva; Desativar conserva seus pontos. Pan desligado volta ao slider do mixer. FX desligado mantém o preset sempre ativo. Só a música na grade recebe automação: Ouvir célula e fontes ao vivo ficam intactos. Projeto, recuperação e WAV usam a mesma decisão. O botão CURVAS abre o editor visual compacto para arrastar os três pontos. Escolha um preset audível antes de testar FX.",
                "PER-STEM AUTOMATION — In the FX menu, Volume · automação uses three levels at 0%, 50% and 100% of the grid. Pan · automação moves smoothly among left, center and right at the same points. FX · automação decides whether the current preset sounds in the entrance, middle and ending; changes occur at 25% and 75%, smoothly transitioning to bypass, while a tail may finish naturally. Editing enables a curve; disabling retains its points. Disabled pan returns to the mixer slider. Disabled FX automation keeps the preset active throughout. Only grid playback is automated; cell preview and live sources remain unchanged. Project, recovery and WAV make the same decisions. CURVAS opens the compact visual editor for dragging the three points. Choose an audible preset first. No mouse recording or internal-parameter automation.",
                "AUTOMATION PAR STEM — Dans le menu FX, Volume · automação emploie trois niveaux à 0, 50 et 100 % de la grille. Pan · automação se déplace progressivement entre gauche, centre et droite aux mêmes points. FX · automação active le preset courant dans l’entrée, le milieu ou la fin ; changements à 25 et 75 %, transition douce vers bypass, avec une traîne qui peut finir naturellement. Éditer active la courbe ; désactiver conserve ses points. Pan désactivé retrouve le curseur du mixeur. Automation FX désactivée maintient le preset actif. Seule la grille est automatisée ; préécoute et sources directes restent intactes. Projet, récupération et WAV partagent les décisions. CURVAS ouvre l’éditeur visuel compact pour déplacer les trois points. Choisissez d’abord un preset audible. Sans enregistrement de souris ni automation des paramètres internes.",
                "AUTOMATIZACIÓN POR STEM — En el menú FX, Volume · automação usa tres niveles en 0, 50 y 100 % de la cuadrícula. Pan · automação se mueve suavemente entre izquierda, centro y derecha en los mismos puntos. FX · automação activa el preset actual en entrada, medio o final; cambios en 25 y 75 %, transición suave a bypass, y una cola puede terminar naturalmente. Editar activa la curva; desactivar conserva sus puntos. Pan desactivado recupera el slider del mezclador. Automatización FX desactivada mantiene el preset activo. Solo la cuadrícula se automatiza; preescucha y fuentes directas quedan intactas. Proyecto, recuperación y WAV comparten decisiones. CURVAS abre el editor visual compacto para arrastrar los tres puntos. Elija primero un preset audible. Sin grabación de ratón ni automatización de parámetros internos."
            };
            text += "\n\n" + utf8(automationTips[language]);
            static const char* developments[4] = {
                "RITMO E HARMONIA POR COMPASSO — Fórmula → notas oferece subdivisões por semínima, de semínimas a sextinas. Tercinas/sextinas ficam retas; swing atua em pares binários. Em 6/8, cada compasso ocupa três semínimas, com acentos em dois grupos de três colcheias. Em Acompanhar/Guiar acorde, escolha um grau ou silêncio por compasso; Automático volta à progressão. Só os compassos presentes na frase são usados.\n\nEDITAR SEÇÕES — Em Variar frase, Editar cada seção permite densidade, registro e intensidade. Restaurar seção volta aos valores originais. Comparar alterna original e edição com a mesma seed e variação. Guardar cria outra célula; Atualizar substitui a selecionada e permite desfazer.\n\nFX E DIÁLOGO — Cada stem tem FX: bypass, dois filtros ou filtro/bitcrush. A transição é suavizada e o WAV inclui os efeitos. Em Resposta, escolha Original, espaço livre ou papel/registro/espaço; a pista de destino e o intervalo continuam explícitos. A linha de estado informa quantas respostas foram preparadas.\n\nLEITURA E BANCO — Leitura / glifo escolhe ouvir contorno de alturas, ritmo, intensidade ou tempo invertido, preservando texto/desenho. O fim do menu do banco permite buscar, favoritar e consultar proveniência. Ouvir célula faz a pré-escuta.\n\nFLOATBEAT E AY — Em Fórmula → notas, carregue um exemplo Floatbeat. O prefixo f: usa t em segundos, decimais e sin/cos/abs/tanh, saída -1 a 1 em 8 kHz. Compilar e Ouvir fórmula tocam o exemplo. AY · tons é um estudo parcial de três tons, não emulação fiel: sem ruído/envelope de hardware e com banda limitada.\n\nRETOMADA E TECLADO — A cópia automática inclui a prévia generativa e edições ainda não guardadas. Recuperar mantém o transporte parado; Salvar confirma um projeto separado. Fora dos campos de texto: Espaço toca/para a grade, Esc para, Ctrl/Cmd+S salva e Ctrl/Cmd+O abre. RMS mostra energia média em dBFS, não LUFS.",
                "RHYTHM AND HARMONY PER BAR — Fórmula → notas offers subdivisions per quarter note, from quarters to sextuplets. Triplets/sextuplets remain straight; swing affects binary pairs. A 6/8 bar occupies three quarter units, accented as two groups of three eighths. In Acompanhar/Guiar acorde choose a degree or silence per bar; Automático restores the progression. Only bars present in the phrase are used.\n\nEDIT SECTIONS — In Variar frase, edit density, register and intensity per section. Restore resets that section. Compare toggles original and edits with the same seed and variation. Guardar creates another cell; Atualizar replaces the selected cell with undo.\n\nFX AND DIALOGUE — Each stem has FX: bypass, two filters, or filter/bitcrush. Transitions are smoothed and WAV export includes effects. In Resposta choose Original, free space, or role/register/space; direction and interval remain explicit. Status reports prepared responses.\n\nREADING AND BANK — Leitura / glifo selects pitch contour, rhythm, dynamics or reversed time while preserving text/drawing. The bank menu offers search, favorites and provenance. Ouvir célula previews a cell.\n\nFLOATBEAT AND AY — Load a Floatbeat example in Fórmula → notas. The f: prefix uses t in seconds, decimals and sin/cos/abs/tanh, -1 to 1 output at 8 kHz. Compile then Ouvir fórmula. AY · tons is a partial three-tone study, not faithful emulation: no hardware noise/envelope and a limited bandwidth.\n\nRECOVERY AND KEYS — Automatic copies include unsaved generative previews and edits. Recovery keeps transport stopped; Save confirms a separate project. Outside text fields: Space plays/stops the grid, Esc stops, Ctrl/Cmd+S saves and Ctrl/Cmd+O opens. RMS reports average energy in dBFS, not LUFS.",
                "RYTHME ET HARMONIE PAR MESURE — Fórmula → notas propose les subdivisions par noire, jusqu’aux sextolets. Triolets/sextolets restent réguliers ; le swing agit sur les paires binaires. Une mesure 6/8 occupe trois noires, accentuées en deux groupes de trois croches. En Acompanhar/Guiar acorde, choisissez degré ou silence par mesure ; Automático restaure la progression. Seules les mesures présentes sont utilisées.\n\nÉDITER LES SECTIONS — Dans Variar frase, réglez densité, registre et intensité par section. Restaurer rétablit cette section. Comparer alterne original et édition avec les mêmes seed et variation. Guardar crée une cellule ; Atualizar remplace la sélection avec annulation.\n\nFX ET DIALOGUE — Chaque stem a FX : bypass, deux filtres ou filtre/bitcrush. Les transitions sont lissées et le WAV inclut les effets. Resposta propose Original, espace libre ou rôle/registre/espace ; direction et intervalle restent explicites. Le statut indique les réponses préparées.\n\nLECTURE ET BANQUE — Leitura / glifo choisit contour, rythme, intensité ou temps inversé en préservant texte/dessin. Le menu de banque propose recherche, favoris et provenance. Ouvir célula écoute la sélection.\n\nFLOATBEAT ET AY — Chargez un exemple Floatbeat dans Fórmula → notas. Le préfixe f: utilise t en secondes, décimales et sin/cos/abs/tanh, sortie -1 à 1 à 8 kHz. Compilez puis Ouvir fórmula. AY · tons est une étude partielle de trois tons, sans bruit/enveloppe matériel et à bande limitée, pas une émulation fidèle.\n\nREPRISE ET CLAVIER — La copie automatique inclut les aperçus génératifs et éditions non sauvegardés. La reprise reste à l’arrêt ; Salvar confirme un projet séparé. Hors champs de texte : Espace joue/arrête la grille, Échap arrête, Ctrl/Cmd+S enregistre, Ctrl/Cmd+O ouvre. RMS indique l’énergie moyenne en dBFS, pas LUFS.",
                "RITMO Y ARMONÍA POR COMPÁS — Fórmula → notas ofrece subdivisiones por negra hasta seisillos. Tresillos/seisillos siguen rectos; swing actúa en pares binarios. Un 6/8 ocupa tres negras, con acentos en dos grupos de tres corcheas. En Acompanhar/Guiar acorde elija grado o silencio por compás; Automático restaura la progresión. Solo se usan los compases presentes.\n\nEDITAR SECCIONES — En Variar frase ajuste densidad, registro e intensidad por sección. Restaurar recupera los valores de esa sección. Comparar alterna original y edición con la misma seed y variación. Guardar crea otra célula; Atualizar sustituye la seleccionada con deshacer.\n\nFX Y DIÁLOGO — Cada stem tiene FX: bypass, dos filtros o filtro/bitcrush. Transiciones suavizadas y efectos incluidos en WAV. Resposta ofrece Original, espacio libre o función/registro/espacio; dirección e intervalo siguen explícitos. El estado indica las respuestas preparadas.\n\nLECTURA Y BANCO — Leitura / glifo elige contorno, ritmo, intensidad o tiempo invertido preservando texto/dibujo. El menú del banco ofrece búsqueda, favoritos y procedencia. Ouvir célula reproduce la selección.\n\nFLOATBEAT Y AY — Cargue un ejemplo Floatbeat en Fórmula → notas. El prefijo f: usa t en segundos, decimales y sin/cos/abs/tanh, salida -1 a 1 a 8 kHz. Compile y pulse Ouvir fórmula. AY · tons es un estudio parcial de tres tonos, sin ruido/envolvente de hardware y de banda limitada; no es emulación fiel.\n\nRETOMAR Y TECLADO — La copia automática incluye previas generativas y ediciones sin guardar. Recuperar deja el transporte parado; Salvar confirma un proyecto separado. Fuera del texto: Espacio toca/para la cuadrícula, Esc para, Ctrl/Cmd+S guarda y Ctrl/Cmd+O abre. RMS mide energía media en dBFS, no LUFS."
            };
            text += "\n\n" + utf8(developments[language]);
            const char* compositionSeedTips[] = {
                "COMPOSIÇÃO POR SEED — Em Salvar → Gerar variação por seed, escolha seed, nível e perfil de travas. Revisar proposta mostra as mudanças antes de aplicar. A música atual é guardada como versão recuperável; salve o .risco para persistir a nova. Assistida altera células geradas e andamento; Conjunto acrescenta papéis/métrica dos stems e diálogo; Ampla inclui timbre, FX e mixagem por stem. MASTER fica travado por padrão e a proteção técnica nunca é sorteada. Manual continua sendo o fluxo normal de edição.",
                "SEED COMPOSITION — In Save → Gerar variação por seed, choose a seed, level and lock profile. Revisar proposta lists the changes before applying. The current song is saved as a recoverable version; save the .risco file to persist the new one. Assisted changes generated cells and tempo; Ensemble adds stem roles/meters and dialogue; Wide includes timbre, FX and stem mix. MASTER is locked by default and output safety is never randomized. Manual remains the regular editing workflow.",
                "COMPOSITION PAR SEED — Dans Salvar → Gerar variação por seed, choisissez seed, niveau et profil de verrous. Revisar proposta présente les changements avant application. Le morceau actuel devient une version récupérable ; enregistrez le fichier .risco pour conserver la nouvelle. Assistida modifie cellules générées et tempo ; Conjunto ajoute rôles/métriques des stems et dialogue ; Ampla inclut timbre, FX et mixage par stem. MASTER est verrouillé par défaut et la protection de sortie n’est jamais tirée au sort. Manual reste le flux normal d’édition.",
                "COMPOSICIÓN POR SEED — En Salvar → Gerar variação por seed, elija seed, nivel y perfil de bloqueos. Revisar proposta muestra los cambios antes de aplicarlos. La canción actual queda como versión recuperable; guarde el archivo .risco para conservar la nueva. Assistida cambia células generadas y tempo; Conjunto añade roles/métrica de stems y diálogo; Ampla incluye timbre, FX y mezcla por stem. MASTER se bloquea por defecto y la protección de salida nunca se sortea. Manual sigue siendo el flujo normal de edición."
            };
            text += "\n\n" + utf8(compositionSeedTips[language]);
            static const char* seedTrackTips[4] = {
                "FAIXA COMPLETA POR SEED — Em Salvar → Gerar faixa completa por seed, informe uma seed. Revise andamento, duração e os canais melodia, harmonia, baixo, ritmo e textura; escolha um novo arquivo .risco. A faixa só substitui a composição aberta depois de salvar e reabrir o projeto.",
                "FULL TRACK FROM SEED — In Save → Gerar faixa completa por seed, enter a seed. Review tempo, duration and the melody, harmony, bass, rhythm and texture channels; choose a new .risco file. The track replaces the open composition only after saving and reopening the project.",
                "MORCEAU COMPLET PAR SEED — Dans Salvar → Gerar faixa completa par seed, saisissez une seed. Vérifiez le tempo, la durée et les canaux mélodie, harmonie, basse, rythme et texture ; choisissez un nouveau fichier .risco.",
                "PIEZA COMPLETA POR SEED — En Salvar → Gerar faixa completa por seed, introduzca una seed. Revise el tempo, la duración y los canales de melodía, armonía, bajo, ritmo y textura; elija un archivo .risco nuevo."
            };
            text += "\n\n" + utf8(seedTrackTips[language]);
            static const char* albumTips[4] = {
                "MODO ÁLBUM — Em Salvar → Gerar projetos de álbum, defina título, seed-mãe, quantidade, BPM central, amplitude e contorno. Opcionalmente, informe perfis por faixa em sequência desde a faixa 1: O (original), R (parentesco), C (contraste), - (perfil geral). Revise o plano e escolha a pasta-pai; os projetos .risco independentes são criados em sequência com checkpoint atômico em album.risco-manifest. Depois, Salvar → Renderizar WAV do álbum cria WAV PCM24/48 kHz com MASTER e mede LUFS/true peak no manifesto. Para retomar qualquer fila, selecione a mesma pasta. Os trabalhos são sequenciais, canceláveis entre faixas e retomáveis; o render reserva 5 GB além do espaço estimado. A audição autoral e a masterização de álbum permanecem pendentes.",
                "ALBUM MODE — In Save → Gerar projetos de álbum, set the title, master seed, track count, center BPM, spread and contour. Optionally enter per-track profiles as a sequence starting at track 1: O (original), R (related), C (contrasting), - (album default). Review the plan and choose a parent folder; independent .risco projects are created sequentially with atomic checkpoints in album.risco-manifest. Then Save → Renderizar WAV do álbum creates PCM24/48 kHz WAVs with MASTER and records LUFS/true peak in the manifest. To resume either queue, select the same folder. Jobs are sequential, cancelable between tracks and resumable; the render reserves 5 GB beyond estimated space. Authorial listening and album mastering remain pending.",
                "MODE ALBUM — Dans Salvar → Gerar projetos de álbum, définissez le titre, la seed mère, le nombre de pistes, le BPM central, l’amplitude et le contour. Facultativement, saisissez les profils par piste dans une séquence à partir de la piste 1 : O (original), R (parenté), C (contraste), - (profil général). Vérifiez le plan et choisissez le dossier parent ; les projets .risco indépendants sont créés en séquence avec checkpoints atomiques dans album.risco-manifest. Ensuite, Salvar → Renderizar WAV do álbum crée des WAV PCM24/48 kHz avec MASTER et inscrit LUFS/true peak dans le manifeste. Pour reprendre une file, choisissez le même dossier. Les tâches sont séquentielles, annulables entre pistes et reprenables ; le rendu réserve 5 Go au-delà de l’espace estimé. L’écoute d’auteur et le mastering d’album restent à faire.",
                "MODO ÁLBUM — En Salvar → Gerar projetos de álbum, defina título, seed madre, cantidad, BPM central, amplitud y contorno. Opcionalmente, escriba perfiles por pieza en secuencia desde la pieza 1: O (original), R (afinidad), C (contraste), - (perfil general). Revise el plan y elija la carpeta padre; los proyectos .risco independientes se crean en secuencia con checkpoints atómicos en album.risco-manifest. Luego, Salvar → Renderizar WAV do álbum crea WAV PCM24/48 kHz con MASTER y registra LUFS/true peak en el manifiesto. Para reanudar una cola, elija la misma carpeta. Los trabajos son secuenciales, cancelables entre pistas y reanudables; el render reserva 5 GB además del espacio estimado. La escucha autoral y la masterización del álbum siguen pendientes."
            };
            text += "\n\n" + utf8(albumTips[language]);
            static const juce::String glitchTips[4] = {
                utf8("GLITCH MATEMÁTICO — No seletor de origem, escolha Glitch: máscara, Glitch: XOR ou Glitch: wrap. Selecione no banco uma célula Bytebeat como origem, ajuste a intensidade de 1 a 100 e clique Aplicar glitch. O Risco cria uma nova fórmula, preserva a origem e a insere no fim da grade, no STEM selecionado."),
                "MATH GLITCH — In the source selector, choose Glitch: máscara, Glitch: XOR or Glitch: wrap. Select a Bytebeat cell in the bank as the source, set intensity from 1 to 100 and press Aplicar glitch. Risco creates a new formula, preserves the source and places it at the end of the selected stem.",
                utf8("GLITCH MATHÉMATIQUE — Dans le sélecteur de source, choisissez Glitch: máscara, Glitch: XOR ou Glitch: wrap. Sélectionnez une cellule Bytebeat dans la banque comme source, réglez l’intensité de 1 à 100 et cliquez sur Aplicar glitch. Risco crée une nouvelle formule, préserve la source et place le résultat à la fin du stem sélectionné."),
                utf8("GLITCH MATEMÁTICO — En el selector de origen, elija Glitch: máscara, Glitch: XOR o Glitch: wrap. Seleccione una célula Bytebeat del banco como origen, ajuste la intensidad de 1 a 100 y pulse Aplicar glitch. Risco crea una fórmula nueva, conserva el origen y la coloca al final del stem seleccionado.")
            };
            text += "\n\n" + glitchTips[std::clamp(language, 0, 3)];
        }
        body.setDocument(text, language, topic);
    }

    std::array<juce::TextButton, 3> topics;
    juce::TextButton closeButton;
    HelpReader body;
    int language = 0;
    int topic = 0;
};

enum class AutomationCurveChoice {
    volume = 1,
    pan,
    fx,
    filter,
    delayMix,
    delayFeedback,
    reverbMix,
    reverbDecay,
    bitcrush
};

class AutomationEditor final : public juce::Component {
public:
    AutomationEditor() {
        setWantsKeyboardFocus(true);
        setAlwaysOnTop(true);
        addAndMakeVisible(stemSelect);
        stemSelect.addItem("Stem 1", 1);
        stemSelect.addItem("Stem 2", 2);
        stemSelect.addItem("Stem 3", 3);
        stemSelect.addItem(utf8("Stem 4 · Ritmo"), 4);
        stemSelect.addItem(utf8("Stem 5 · Textura"), 5);
        stemSelect.setSelectedId(1, juce::dontSendNotification);
        stemSelect.onChange = [this] {
            if (onStemChanged) onStemChanged(stemSelect.getSelectedId() - 1);
        };
        stemSelect.setTooltip(utf8("Escolha o stem cuja automação será editada."));

        addAndMakeVisible(curveSelect);
        const std::pair<const char*, int> curves[] = {
            {"Volume", int(AutomationCurveChoice::volume)},
            {"Pan", int(AutomationCurveChoice::pan)},
            {"FX ligado/desligado", int(AutomationCurveChoice::fx)},
            {"Filtro · corte", int(AutomationCurveChoice::filter)},
            {"Delay · mistura", int(AutomationCurveChoice::delayMix)},
            {"Delay · realimentação", int(AutomationCurveChoice::delayFeedback)},
            {"Reverb · mistura", int(AutomationCurveChoice::reverbMix)},
            {"Reverb · decaimento", int(AutomationCurveChoice::reverbDecay)},
            {"Bitcrush · bits", int(AutomationCurveChoice::bitcrush)}};
        for (const auto& curve : curves) curveSelect.addItem(utf8(curve.first), curve.second);
        curveSelect.setSelectedId(int(AutomationCurveChoice::volume), juce::dontSendNotification);
        curveSelect.onChange = [this] {
            if (onCurveChanged) onCurveChanged(curveSelect.getSelectedId());
        };
        curveSelect.setTooltip(utf8("Escolha uma curva. Os pontos ficam em 0%, 50% e 100% da grade."));

        addAndMakeVisible(enabledButton);
        enabledButton.onClick = [this] {
            if (onEnabledChanged) onEnabledChanged(!enabled);
        };
        addAndMakeVisible(closeButton);
        closeButton.setButtonText(utf8("Fechar"));
        closeButton.onClick = [this] { if (onClose) onClose(); };
    }

    std::function<void(int)> onStemChanged;
    std::function<void(int)> onCurveChanged;
    std::function<void(int, float)> onPointChanged;
    std::function<void(bool)> onEnabledChanged;
    std::function<void()> onClose;

    int selectedStem() const noexcept { return stemSelect.getSelectedId() - 1; }
    int selectedCurve() const noexcept { return curveSelect.getSelectedId(); }

    void setSelection(int stem, int curve) {
        stemSelect.setSelectedId(stem + 1, juce::dontSendNotification);
        curveSelect.setSelectedId(curve, juce::dontSendNotification);
    }

    void setState(const std::array<float, 3>& nextValues, bool nextEnabled,
                  const juce::String& title, const juce::String& range,
                  const std::array<juce::String, 3>& nextLabels) {
        values = nextValues;
        enabled = nextEnabled;
        graphTitle = title;
        graphRange = range;
        labels = nextLabels;
        enabledButton.setButtonText(enabled ? utf8("Automação: ligada") : utf8("Automação: desligada"));
        enabledButton.setToggleState(enabled, juce::dontSendNotification);
        repaint();
    }

    void resized() override {
        const auto area = getLocalBounds().reduced(24);
        stemSelect.setBounds(area.getX(), area.getY() + 42, 112, 30);
        curveSelect.setBounds(area.getX() + 122, area.getY() + 42, 205, 30);
        enabledButton.setBounds(area.getX() + 337, area.getY() + 42, 180, 30);
        closeButton.setBounds(area.getRight() - 94, area.getY() + 42, 94, 30);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xd908101b));
        const auto panel = getLocalBounds().toFloat().reduced(12.0f);
        g.setColour(juce::Colour(0xff101b2d));
        g.fillRoundedRectangle(panel, 10.0f);
        g.setColour(juce::Colour(0xff55d6e8));
        g.drawRoundedRectangle(panel.reduced(0.5f), 10.0f, 1.5f);
        g.setColour(juce::Colour(0xffdce9f1));
        g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 18.0f, juce::Font::bold)));
        g.drawText(utf8("EDITOR VISUAL DE AUTOMAÇÕES"), panel.getX() + 18.0f, panel.getY() + 14.0f,
                   panel.getWidth() - 36.0f, 24.0f, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8faabd));
        g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 11.0f, juce::Font::plain)));
        g.drawText(graphTitle + utf8(" · três pontos fixos · ") + graphRange,
                   panel.getX() + 18.0f, panel.getY() + 72.0f, panel.getWidth() - 36.0f, 18.0f,
                   juce::Justification::centredLeft);

        const auto graph = juce::Rectangle<float>(panel.getX() + 72.0f, panel.getY() + 112.0f,
                                                   panel.getWidth() - 104.0f, panel.getHeight() - 190.0f);
        g.setColour(juce::Colour(0xff071522));
        g.fillRoundedRectangle(graph, 6.0f);
        for (int i = 1; i < 4; ++i) {
            const float y = graph.getY() + graph.getHeight() * float(i) / 4.0f;
            g.setColour(juce::Colour(0xff1d3749));
            g.drawHorizontalLine(juce::roundToInt(y), graph.getX(), graph.getRight());
        }
        for (int i = 0; i < 3; ++i) {
            const float x = graph.getX() + graph.getWidth() * float(i) * 0.5f;
            g.setColour(juce::Colour(0xff345467));
            g.drawVerticalLine(juce::roundToInt(x), graph.getY(), graph.getBottom());
            g.setColour(juce::Colour(0xff9ab2c1));
            g.drawText(i == 0 ? "0%" : i == 1 ? "50%" : "100%", x - 22.0f, graph.getBottom() + 8.0f,
                       44.0f, 16.0f, juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff9ab2c1));
        g.drawText("100%", panel.getX() + 8.0f, graph.getY() - 8.0f, 56.0f, 16.0f, juce::Justification::centredRight);
        g.drawText("0%", panel.getX() + 8.0f, graph.getBottom() - 8.0f, 56.0f, 16.0f, juce::Justification::centredRight);
        g.setColour(enabled ? juce::Colour(0xff65d9e8) : juce::Colour(0xff788998));
        juce::Path curve;
        for (int i = 0; i < 3; ++i) {
            const auto point = graphPoint(graph, i);
            if (i == 0) curve.startNewSubPath(point);
            else curve.lineTo(point);
        }
        g.strokePath(curve, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        for (int i = 0; i < 3; ++i) {
            const auto point = graphPoint(graph, i);
            g.setColour(juce::Colour(0xff101b2d));
            g.fillEllipse(point.x - 10.0f, point.y - 10.0f, 20.0f, 20.0f);
            g.setColour(enabled ? juce::Colour(0xffffc65c) : juce::Colour(0xff8b9ba8));
            g.fillEllipse(point.x - 6.0f, point.y - 6.0f, 12.0f, 12.0f);
            g.setColour(juce::Colour(0xffe6f5fa));
            g.drawText(labels[static_cast<size_t>(i)], point.x - 52.0f, point.y - 31.0f,
                       104.0f, 18.0f, juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff8faabd));
        g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 11.0f, juce::Font::plain)));
        g.drawText(utf8("Arraste os pontos. A edição ativa a curva e permanece reversível pelo menu FX."),
                   panel.getX() + 18.0f, panel.getBottom() - 34.0f, panel.getWidth() - 36.0f, 20.0f,
                   juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& event) override {
        const auto graph = graphBounds();
        int nearest = -1;
        float distance = 26.0f;
        for (int i = 0; i < 3; ++i) {
            const auto point = graphPoint(graph, i);
            const float d = point.getDistanceFrom(event.position);
            if (d < distance) { distance = d; nearest = i; }
        }
        if (nearest >= 0) {
            grabbedPoint = nearest;
            updatePoint(event.position.y, graph);
        }
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (grabbedPoint >= 0) updatePoint(event.position.y, graphBounds());
    }
    void mouseUp(const juce::MouseEvent&) override { grabbedPoint = -1; }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getKeyCode() == juce::KeyPress::escapeKey && onClose) {
            onClose();
            return true;
        }
        return false;
    }

private:
    juce::Rectangle<float> graphBounds() const {
        const auto panel = getLocalBounds().toFloat().reduced(12.0f);
        return {panel.getX() + 72.0f, panel.getY() + 112.0f,
                panel.getWidth() - 104.0f, panel.getHeight() - 190.0f};
    }
    juce::Point<float> graphPoint(const juce::Rectangle<float>& graph, int point) const {
        return {graph.getX() + graph.getWidth() * float(point) * 0.5f,
                graph.getBottom() - std::clamp(values[static_cast<size_t>(point)], 0.0f, 1.0f) * graph.getHeight()};
    }
    void updatePoint(float y, const juce::Rectangle<float>& graph) {
        if (grabbedPoint < 0 || y < graph.getY() || y > graph.getBottom()) return;
        const float value = std::clamp((graph.getBottom() - y) / graph.getHeight(), 0.0f, 1.0f);
        values[static_cast<size_t>(grabbedPoint)] = value;
        if (onPointChanged) onPointChanged(grabbedPoint, value);
        repaint();
    }

    juce::ComboBox stemSelect, curveSelect;
    juce::TextButton enabledButton, closeButton;
    std::array<float, 3> values{{1.0f, 1.0f, 1.0f}};
    std::array<juce::String, 3> labels{{"100%", "100%", "100%"}};
    juce::String graphTitle = utf8("Volume · Stem 1"), graphRange = "0–100%";
    bool enabled = false;
    int grabbedPoint = -1;
};

class TemporalGestureEditor final : public juce::Component {
public:
    TemporalGestureEditor() {
        setAlwaysOnTop(true);
        setWantsKeyboardFocus(true);
        addAndMakeVisible(closeButton);
        closeButton.setButtonText(utf8("Fechar"));
        closeButton.onClick = [this] { if (onClose) onClose(); };
    }

    std::function<void(int, float)> onPointChanged;
    std::function<void()> onClose;

    void setValues(const std::array<float, 3>& next) {
        values = next;
        repaint();
    }

    void resized() override {
        const auto area = getLocalBounds().reduced(24);
        closeButton.setBounds(area.getRight() - 94, area.getY() + 42, 94, 30);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xd908101b));
        const auto panel = getLocalBounds().toFloat().reduced(12.0f);
        g.setColour(juce::Colour(0xff101b2d));
        g.fillRoundedRectangle(panel, 10.0f);
        g.setColour(juce::Colour(0xffffc65c));
        g.drawRoundedRectangle(panel.reduced(0.5f), 10.0f, 1.5f);
        g.setColour(juce::Colour(0xffdce9f1));
        g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 18.0f, juce::Font::bold)));
        g.drawText(utf8("CURVA AUTORAL DE GESTO"), panel.getX() + 18.0f, panel.getY() + 14.0f,
                   panel.getWidth() - 36.0f, 24.0f, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8faabd));
        g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 11.0f, juce::Font::plain)));
        g.drawText(utf8("três pontos · início, meio e fim · preserva a duração total"),
                   panel.getX() + 18.0f, panel.getY() + 72.0f, panel.getWidth() - 36.0f, 18.0f,
                   juce::Justification::centredLeft);
        const auto graph = graphBounds();
        g.setColour(juce::Colour(0xff071522));
        g.fillRoundedRectangle(graph, 6.0f);
        for (int i = 1; i < 4; ++i) {
            const float y = graph.getY() + graph.getHeight() * float(i) / 4.0f;
            g.setColour(juce::Colour(0xff1d3749));
            g.drawHorizontalLine(juce::roundToInt(y), graph.getX(), graph.getRight());
        }
        for (int i = 0; i < 3; ++i) {
            const float x = graph.getX() + graph.getWidth() * float(i) * 0.5f;
            g.setColour(juce::Colour(0xff345467));
            g.drawVerticalLine(juce::roundToInt(x), graph.getY(), graph.getBottom());
            g.setColour(juce::Colour(0xff9ab2c1));
            g.drawText(i == 0 ? "0%" : i == 1 ? "50%" : "100%", x - 22.0f, graph.getBottom() + 8.0f,
                       44.0f, 16.0f, juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff9ab2c1));
        g.drawText("100%", panel.getX() + 8.0f, graph.getY() - 8.0f, 56.0f, 16.0f, juce::Justification::centredRight);
        g.drawText("0%", panel.getX() + 8.0f, graph.getBottom() - 8.0f, 56.0f, 16.0f, juce::Justification::centredRight);
        juce::Path curve;
        for (int i = 0; i < 3; ++i) {
            const auto point = graphPoint(graph, i);
            if (i == 0) curve.startNewSubPath(point); else curve.lineTo(point);
        }
        g.setColour(juce::Colour(0xffffc65c));
        g.strokePath(curve, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        for (int i = 0; i < 3; ++i) {
            const auto point = graphPoint(graph, i);
            g.setColour(juce::Colour(0xff101b2d));
            g.fillEllipse(point.x - 10.0f, point.y - 10.0f, 20.0f, 20.0f);
            g.setColour(juce::Colour(0xffffc65c));
            g.fillEllipse(point.x - 6.0f, point.y - 6.0f, 12.0f, 12.0f);
            g.setColour(juce::Colour(0xffe6f5fa));
            g.drawText(juce::String(int(std::lround(values[static_cast<size_t>(i)] * 100.0f))) + "%",
                       point.x - 52.0f, point.y - 31.0f, 104.0f, 18.0f, juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff8faabd));
        g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 11.0f, juce::Font::plain)));
        g.drawText(utf8("Arraste o ponto do meio. Os extremos ficam presos a 0% e 100%; a curva é reversível."),
                   panel.getX() + 18.0f, panel.getBottom() - 34.0f, panel.getWidth() - 36.0f, 20.0f,
                   juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& event) override {
        const auto graph = graphBounds();
        const auto middle = graphPoint(graph, 1);
        if (middle.getDistanceFrom(event.position) < 30.0f) grabbedPoint = 1;
        if (grabbedPoint >= 0) updatePoint(event.position.y, graph);
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (grabbedPoint >= 0) updatePoint(event.position.y, graphBounds());
    }
    void mouseUp(const juce::MouseEvent&) override { grabbedPoint = -1; }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getKeyCode() == juce::KeyPress::escapeKey && onClose) { onClose(); return true; }
        return false;
    }

private:
    juce::Rectangle<float> graphBounds() const {
        const auto panel = getLocalBounds().toFloat().reduced(12.0f);
        return {panel.getX() + 72.0f, panel.getY() + 112.0f,
                panel.getWidth() - 104.0f, panel.getHeight() - 190.0f};
    }
    juce::Point<float> graphPoint(const juce::Rectangle<float>& graph, int point) const {
        return {graph.getX() + graph.getWidth() * float(point) * 0.5f,
                graph.getBottom() - std::clamp(values[static_cast<size_t>(point)], 0.0f, 1.0f) * graph.getHeight()};
    }
    void updatePoint(float y, const juce::Rectangle<float>& graph) {
        if (grabbedPoint != 1 || y < graph.getY() || y > graph.getBottom()) return;
        values[1] = std::clamp((graph.getBottom() - y) / graph.getHeight(), 0.02f, 0.98f);
        if (onPointChanged) onPointChanged(1, values[1]);
        repaint();
    }

    juce::TextButton closeButton;
    std::array<float, 3> values{{0.0f, 0.5f, 1.0f}};
    int grabbedPoint = -1;
};

class AutoSettingsButton final : public juce::TextButton {
public:
    std::function<void()> onContextMenu;
    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isRightButtonDown()) {
            if (onContextMenu) onContextMenu();
            return;
        }
        juce::TextButton::mouseDown(event);
    }
};

class MainComponent final : public juce::AudioAppComponent, private juce::Timer {
public:
    using GridState = RecoveryGridState;
    struct CompositionVersion {
        juce::String name;
        risco::Song song;
        int parentIndex = -1;
    };

    MainComponent() {
        marqueeScrollStartMillis = juce::Time::getMillisecondCounter();
        const auto savedLibraryCatalog = readProjectLibraryCatalog(libraryCatalogFile());
        librarySourcePaths = savedLibraryCatalog.sources;
        libraryGroups = savedLibraryCatalog.groups;
        setLookAndFeel(&retroLookAndFeel);
        setWantsKeyboardFocus(true);
        addAndMakeVisible(ascii);
        ascii.setVisible(false);
        ascii.setMultiLine(true);
        ascii.setReturnKeyStartsNewLine(true);
        ascii.setTextToShowWhenEmpty(utf8("Digite aqui — o som acompanha o texto"), juce::Colours::grey);
        ascii.setInputRestrictions(4096);
        ascii.onTextChange = [this] {
            currentPhraseGenerated = false;
            marqueeScrollStartMillis = juce::Time::getMillisecondCounter();
            updateLiveDrawing(false);
        };
        ascii.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0c1728));
        ascii.setColour(juce::TextEditor::textColourId, juce::Colour(0xffe0f6ff));
        ascii.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff315671));
        ascii.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xffffc65c));
        addAndMakeVisible(formulaInput);
        formulaInput.setInputRestrictions(128);
        formulaInput.setTextToShowWhenEmpty(utf8("Bytebeat em t ou f: Floatbeat"), juce::Colours::grey);
        formulaInput.setTooltip(utf8("Bytebeat: t é contador de amostras e a aritmética é inteira. Floatbeat: comece com f:, t em segundos, decimais e sin/cos/abs/tanh; exemplo f:sin(t*1382.3)*.4. Compilar verifica a gramática. Exemplos no menu Fórmula → notas."));
        formulaInput.setText("t + (t >> 4)", juce::dontSendNotification);
        addAndMakeVisible(formulaGridStatus);
        formulaGridStatus.setColour(juce::Label::backgroundColourId, juce::Colour(0xff14283a));
        formulaGridStatus.setColour(juce::Label::textColourId, juce::Colour(0xffdce9f1));
        formulaGridStatus.setColour(juce::Label::outlineColourId, juce::Colour(0xff315671));
        formulaGridStatus.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 12.0f, juce::Font::bold)));
        formulaGridStatus.setJustificationType(juce::Justification::centredLeft);
        formulaGridStatus.setBorderSize({1, 8, 1, 8});
        formulaGridStatus.setTooltip(utf8("Mostra se a fórmula escrita está só no editor, compilada, guardada no banco ou inserida na grade."));
        formulaGridStatus.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(applyFormula);
        addAndMakeVisible(formulaToNotesButton);
        addAndMakeVisible(saveFormulaNotesButton);
        addAndMakeVisible(formulaScaleSelect);
        addAndMakeVisible(formulaTonicSelect);
        addAndMakeVisible(formulaMotionSelect);
        addAndMakeVisible(seedSource);
        addAndMakeVisible(seedInput);
        addAndMakeVisible(generateFormulaButton);
        addAndMakeVisible(repositorySelect);
        addAndMakeVisible(reuseFormulaButton);
        addAndMakeVisible(importCatalogButton);
        addAndMakeVisible(automationEditorButton);
        automationEditorButton.setTooltip(utf8("Abrir o editor visual compacto das automações do stem. Três pontos fixos, persistidos no V69."));
        automationEditorButton.onClick = [this] {
            refreshAutomationEditor();
            automationEditor.setVisible(true);
            automationEditor.toFront(true);
            automationEditor.grabKeyboardFocus();
        };
        addAndMakeVisible(automationEditor);
        automationEditor.setVisible(false);
        automationEditor.onStemChanged = [this](int) { refreshAutomationEditor(); };
        automationEditor.onCurveChanged = [this](int) { refreshAutomationEditor(); };
        automationEditor.onPointChanged = [this](int point, float value) {
            applyAutomationEditorPoint(point, value);
        };
        automationEditor.onEnabledChanged = [this](bool enabled) {
            setAutomationEditorEnabled(enabled);
        };
        automationEditor.onClose = [this] {
            automationEditor.setVisible(false);
            grabKeyboardFocus();
        };
        addAndMakeVisible(gestureEditor);
        gestureEditor.setVisible(false);
        gestureEditor.onPointChanged = [this](int, float value) {
            autoRouteButton.setToggleState(false, juce::dontSendNotification);
            inputGestureCurve.values[1] = std::clamp(value, 0.02f, 0.98f);
            inputGestureCurve.enabled = true;
            if (inputInterpretationChoice < 9 || inputInterpretationChoice > 13) inputInterpretationChoice = 8;
            updateLiveDrawing(true);
            status.setText(utf8("Gesto autoral · ponto do meio em ") +
                juce::String(int(std::lround(inputGestureCurve.values[1] * 100.0f))) + "%.",
                juce::dontSendNotification);
        };
        gestureEditor.onClose = [this] {
            gestureEditor.setVisible(false);
            grabKeyboardFocus();
        };
        addAndMakeVisible(autoMode);
        addAndMakeVisible(responseModeSelect);
        addAndMakeVisible(varyPhraseButton);
        addAndMakeVisible(autoPhraseButton);
        addAndMakeVisible(recButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(masterSlider);
        addAndMakeVisible(masterLabel);
        addAndMakeVisible(bpmSlider);
        addAndMakeVisible(bpmLabel);
        addAndMakeVisible(meterSelect);
        addAndMakeVisible(bitDepthSelect);
        addAndMakeVisible(timbreSelect);
        addAndMakeVisible(shapeSelect);
        addAndMakeVisible(shapeDriveLabel);
        addAndMakeVisible(shapeDriveSlider);
        addAndMakeVisible(fontSelect);
        addAndMakeVisible(languageSelect);
        languageSelect.addItem("PT", 1); languageSelect.addItem("EN", 2);
        languageSelect.addItem("FR", 3); languageSelect.addItem("ES", 4);
        languageSelect.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(aboutButton);
        addAndMakeVisible(tutorialButton);
        addAndMakeVisible(learnButton);
        addAndMakeVisible(helpOverlay);
        helpOverlay.setVisible(false);
        aboutButton.onClick = [this] { openHelp(0); };
        tutorialButton.onClick = [this] { openHelp(1); };
        learnButton.onClick = [this] { openHelp(2); };
        languageSelect.onChange = [this] {
            updateInterfaceLanguage();
        };
        fontSelect.addItem(utf8("Fonte: padrão"), 1);
        fontSelect.addItem(utf8("Fonte: terminal"), 2);
        fontSelect.addItem(utf8("Fonte: geométrica"), 3);
        fontSelect.setSelectedId(2, juce::dontSendNotification);
        fontSelect.onChange = [this] { retroLookAndFeel.setFontStyle(fontSelect.getSelectedId() - 1); sendLookAndFeelChange(); repaint(); };
        addAndMakeVisible(ledSourceButton);
        ledSourceButton.setTooltip(utf8("Abre o texto neste quadro; em seguida, use Desenhar para voltar ao traço."));
        ledSourceButton.onClick = [this] {
            setTextEntryMode(!textEntryMode, true);
        };
        shapeSelect.addItem("Shape: desligado", 1);
        shapeSelect.addItem("Shape: tanh", 2);
        shapeSelect.addItem(utf8("Shape: cúbica"), 3);
        shapeSelect.addItem("Shape: dobra", 4);
        shapeSelect.setSelectedId(song.shapeMode + 1, juce::dontSendNotification);
        shapeSelect.onChange = [this] { song.shapeMode = shapeSelect.getSelectedId() - 1; renderingShape.store(song.shapeMode); };
        shapeDriveLabel.setText("DRIVE", juce::dontSendNotification);
        shapeDriveLabel.setColour(juce::Label::textColourId, juce::Colour(0xff17283a));
        shapeDriveLabel.setJustificationType(juce::Justification::centred);
        shapeDriveSlider.setTooltip(utf8("Intensidade do drive do Shape."));
        shapeDriveSlider.setRange(0.1, 8.0, 0.05);
        shapeDriveSlider.setValue(song.shapeDrive, juce::dontSendNotification);
        shapeDriveSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1b4765));
        shapeDriveSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe0f6ff));
        shapeDriveSlider.onValueChange = [this] { song.shapeDrive = float(shapeDriveSlider.getValue()); renderingDrive.store(song.shapeDrive); };
        refreshTimbreChoices();
        timbreSelect.setTooltip(utf8("Escolha o timbre e, abaixo, seus ajustes. Menus AY controlam o estudo expandido; menus Histórico controlam apenas AY-3-8910/YM2149: mixer, período de ruído e envelope compartilhados. Cada stem mantém estado próprio. Fórmulas diretas permanecem independentes."));
        timbreSelect.onChange = [this] {
            const int choice = timbreSelect.getSelectedId();
            if (choice >= 500 && choice <= 502) {
                song.historicalAyTexture = choice - 500;
                renderingHistoricalAyTexture.store(song.historicalAyTexture, std::memory_order_release);
                refreshTimbreChoices();
                status.setText(utf8("Mixer histórico atualizado: tom, tom + ruído ou ruído. Atua somente em AY/YM histórico."), juce::dontSendNotification);
                return;
            }
            if (choice >= 601 && choice <= 631) {
                song.historicalAyNoisePeriod = choice - 600;
                renderingHistoricalAyNoisePeriod.store(song.historicalAyNoisePeriod, std::memory_order_release);
                refreshTimbreChoices();
                status.setText(utf8("Período do ruído histórico: ") + juce::String(song.historicalAyNoisePeriod) +
                    utf8(" · registrador compartilhado pelos três canais do stem."), juce::dontSendNotification);
                return;
            }
            if (choice >= 700 && choice <= 704) {
                song.historicalAyEnvelope = choice - 700;
                renderingHistoricalAyEnvelope.store(song.historicalAyEnvelope, std::memory_order_release);
                refreshTimbreChoices();
                status.setText(utf8("Envelope histórico compartilhado atualizado; um novo ataque reinicia a forma."), juce::dontSendNotification);
                return;
            }
            if (choice >= 801 && choice <= 805) {
                constexpr float ramps[] = {0.125f,0.25f,0.5f,1,2};
                song.historicalAyEnvelopeRamp = ramps[choice - 801];
                renderingHistoricalAyEnvelopeRamp.store(song.historicalAyEnvelopeRamp, std::memory_order_release);
                refreshTimbreChoices();
                status.setText(utf8("Rampa do envelope histórico: ") + juce::String(song.historicalAyEnvelopeRamp,3) +
                    utf8(" s aproximados para 16 passos."), juce::dontSendNotification);
                return;
            }
            if (choice >= 300 && choice <= 304) {
                song.ayEnvelopeMode = choice - 300;
                renderingAyEnvelopeMode.store(song.ayEnvelopeMode, std::memory_order_release);
                refreshTimbreChoices();
                status.setText(utf8("Envelope do estudo AY atualizado · reinicia a cada nota; opção Risco original restaura o som anterior. Salvo no projeto/WAV."), juce::dontSendNotification);
                return;
            }
            if (choice >= 401 && choice <= 405) {
                constexpr float ramps[] = {0.125f,0.25f,0.5f,1,2};
                song.ayEnvelopeRamp = ramps[choice - 401];
                renderingAyEnvelopeRamp.store(song.ayEnvelopeRamp, std::memory_order_release);
                refreshTimbreChoices();
                status.setText(utf8("Rampa do envelope AY: ") + juce::String(song.ayEnvelopeRamp, 3) +
                    utf8(" s aproximados · triângulo usa duas rampas; envelope Risco original ignora este ajuste."), juce::dontSendNotification);
                return;
            }
            if (choice >= 101 && choice <= 131) {
                song.ayNoisePeriod = choice - 100;
                renderingAyNoisePeriod.store(song.ayNoisePeriod, std::memory_order_release);
                refreshTimbreChoices();
                status.setText(utf8("Período do ruído AY: ") + juce::String(song.ayNoisePeriod) +
                    utf8(" · ativo em AY misto/ruído; maior valor dá pulsos mais lentos. Salvo no projeto/WAV."), juce::dontSendNotification);
                return;
            }
            song.timbreMode = std::clamp(choice - 1, 0, 6);
            renderingTimbre.store(song.timbreMode, std::memory_order_release);
            if (risco::isHistoricalAyTimbre(song.timbreMode))
                status.setText(song.timbreMode == 5
                    ? utf8("AY-3-8910 histórico · três canais, mixer, ruído e envelope compartilhados por stem. Salvo em V60 e no WAV.")
                    : utf8("YM2149 histórico · três canais, curva DAC YM, mixer, ruído e envelope compartilhados. Salvo em V60 e no WAV."),
                    juce::dontSendNotification);
            if (song.timbreMode > 0) {
                formulaEnabled.store(false, std::memory_order_release);
                functionMode.setToggleState(false, juce::dontSendNotification);
            }
        };
        bitDepthSelect.addItem("8 bits", 8);
        bitDepthSelect.addItem("16 bits", 16);
        bitDepthSelect.setSelectedId(song.bitDepth, juce::dontSendNotification);
        bitDepthSelect.setTooltip(utf8("Quantização do motor de notas original. Bytebeat direto usa 8 bits; Floatbeat usa saída contínua -1 a 1. AY · tons aplica sua quantização própria de amplitude."));
        bitDepthSelect.onChange = [this] {
            song.bitDepth = bitDepthSelect.getSelectedId() == 16 ? 16 : 8;
            renderingBits.store(song.bitDepth, std::memory_order_release);
            refreshPlaybackAfterGridEdit();
        };
        masterLabel.setText("MASTER", juce::dontSendNotification);
        masterLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd6e9f6));
        masterSlider.setRange(0.0, 1.0, 0.01);
        masterSlider.setValue(0.35, juce::dontSendNotification);
        masterSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 24);
        masterSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1b4765));
        masterSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe0f6ff));
        masterSlider.setTooltip(utf8("Volume geral da saída; inicia em 35% para uma escuta mais confortável."));
        masterSlider.onValueChange = [this] {
            const auto value = static_cast<float>(masterSlider.getValue());
            masterGain.store(value, std::memory_order_release);
            song.masterGain = value;
        };
        bpmLabel.setText("BPM", juce::dontSendNotification);
        bpmLabel.setColour(juce::Label::textColourId, juce::Colour(0xff17283a));
        bpmSlider.setRange(40.0, 240.0, 1.0);
        bpmSlider.setValue(song.bpm, juce::dontSendNotification);
        bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 24);
        bpmSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1b4765));
        bpmSlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe0f6ff));
        bpmSlider.setTooltip(utf8("BPM de semínima, inclusive em 6/8. Exemplo: 120 aqui equivale a 80 semínimas pontuadas por minuto em 6/8."));
        bpmSlider.onValueChange = [this] {
            song.bpm = bpmSlider.getValue();
            renderingBpm.store(song.bpm, std::memory_order_release);
            if (loopRangeEndBeat > loopRangeStartBeat) publishLoopRange();
            if (autoPhraseButton.getToggleState())
                nextPhraseMillis = juce::Time::getMillisecondCounter() + phraseCycleMillis();
            if (autoMode.getToggleState())
                nextAutoTick = displayedDirectTime.load(std::memory_order_relaxed) + phraseCycleTicks();
        };
        bpmSlider.onDragEnd = [this] {
            refreshPlaybackAfterGridEdit();
        };
        refreshMeterChoices();
        meterSelect.setTooltip(utf8("Escolha o compasso de referência. Nos submenus Stem 1–5, cada canal pode herdar ou ter ciclo próprio; as linhas aparecem no canal sem alterar o tempo comum em semínimas."));
        meterSelect.onChange = [this] {
            const int choice = meterSelect.getSelectedId();
            if (choice >= 100) {
                const int stem = (choice - 100) / 10;
                const int meter = (choice - 100) % 10;
                if (stem < 0 || stem >= risco::Song::stemCount || (meter != 0 && (meter < 2 || meter > 8))) return;
                song.stemMeter[static_cast<size_t>(stem)] = meter;
                refreshMeterChoices();
                status.setText(utf8("Métrica do stem ") + juce::String(stem + 1) + utf8(": ") +
                    (meter == 0 ? utf8("seguir música") : meter == 8 ? juce::String("6/8") : juce::String(meter) + "/4") +
                    utf8(" · apenas a régua/ciclo da pista muda."), juce::dontSendNotification);
                repaint();
                return;
            }
            const bool compound = choice == 8;
            const int beats = compound ? 3 : choice;
            if (beats < 2 || beats > 7 || (beats == song.beatsPerBar && compound == song.compoundSixEight)) return;
            song.formulaMetricGrouping = -1;
            song.beatsPerBar = beats;
            song.compoundSixEight = compound;
            refreshMeterChoices();
            refreshMetricGroupingOptions();
            if (autoPhraseButton.getToggleState())
                nextPhraseMillis = juce::Time::getMillisecondCounter() + phraseCycleMillis();
            if (autoMode.getToggleState())
                nextAutoTick = displayedDirectTime.load(std::memory_order_relaxed) + phraseCycleTicks();
            refreshPlaybackAfterGridEdit();
            if (activeSong.load(std::memory_order_acquire) == nullptr &&
                active.load(std::memory_order_acquire) != nullptr &&
                !directEnabled.load(std::memory_order_acquire) && !previewingCell)
                updateLiveDrawing(true);
            status.setText(utf8("Compasso ") + (compound ? juce::String("6/8") : juce::String(beats) + "/4") +
                utf8(" · posições e BPM em semínimas; células em compassos seguem o novo tamanho."),
                juce::dontSendNotification);
            repaint();
        };
        for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
            auto& slider = stemPan[static_cast<size_t>(stem)];
            auto& label = stemPanLabel[static_cast<size_t>(stem)];
            auto& gain = stemGain[static_cast<size_t>(stem)];
            auto& mute = stemMute[static_cast<size_t>(stem)];
            auto& solo = stemSolo[static_cast<size_t>(stem)];
            auto& fx = stemFx[static_cast<size_t>(stem)];
            auto& stemRole = stemRoleSelect[static_cast<size_t>(stem)];
            addAndMakeVisible(stemRole);
            stemRole.setSelectedId(int(song.stemRole[static_cast<size_t>(stem)]) + 1,
                                   juce::dontSendNotification);
            stemRole.setTooltip(utf8("Papel pretendido do Stem ") + juce::String(stem + 1) +
                utf8("; a sugestão de posição favorece células com papel correspondente."));
            stemRole.onChange = [this, stem] {
                const int role = stemRoleSelect[static_cast<size_t>(stem)].getSelectedId() - 1;
                if (role >= 0 && role <= int(risco::CellRole::texture)) {
                    song.stemRole[static_cast<size_t>(stem)] = static_cast<risco::CellRole>(role);
                    if (song.responsePolicy >= 2) refreshPlaybackAfterGridEdit();
                }
            };
            addAndMakeVisible(slider);
            addAndMakeVisible(label);
            addAndMakeVisible(gain);
            addAndMakeVisible(mute);
            addAndMakeVisible(solo);
            addAndMakeVisible(fx);
            fx.setButtonText("FX");
            fx.setTooltip(utf8("FX deste stem · filtro, bitcrush, delay ou reverb; automação simples de volume, pan e liga/desliga do efeito. Salvo no projeto e no WAV."));
            fx.onClick = [this, stem] {
                juce::PopupMenu menu;
                const char* names[] = {"Bypass", "Quente · passa-baixas", "Escuro · passa-baixas", "Grão · 8 bits", "Lo-fi · 4 bits", "Delay · colcheia", "Delay · colcheia pontuada", "Delay · semínima", "Reverb · curto", "Reverb · longo"};
                for (int i = 0; i < 10; ++i)
                    menu.addItem(i + 1, utf8(names[i]), true, song.stemEffect[static_cast<size_t>(stem)] == i);
                const auto index = static_cast<size_t>(stem);
                juce::PopupMenu bitcrushMenu;
                constexpr int bitValues[] = {0, 2, 3, 4, 6, 8, 12, 16};
                for (int i = 0; i < 8; ++i)
                    bitcrushMenu.addItem(1600 + i, bitValues[i] == 0 ? utf8("Padrão do preset") : juce::String(bitValues[i]) + utf8(" bits"), true,
                        song.stemBitcrushBits[index] == bitValues[i]);
                menu.addSubMenu(utf8("Bitcrush · profundidade: ") + (song.stemBitcrushBits[index] == 0 ? utf8("padrão") : juce::String(song.stemBitcrushBits[index]) + utf8(" bits")), bitcrushMenu);
                juce::PopupMenu bitcrushAutoMenu;
                const auto& bitcrushCurve = song.stemBitcrushAutomation[index];
                bitcrushAutoMenu.addItem(1700, utf8("Desativar · conservar curva"), true, !bitcrushCurve.enabled);
                bitcrushAutoMenu.addItem(1701, utf8("Entrada · 4 → 8 → 16 bits"));
                bitcrushAutoMenu.addItem(1702, utf8("Saída · 16 → 8 → 4 bits"));
                bitcrushAutoMenu.addItem(1703, utf8("Pulso · 4 → 16 → 4 bits"));
                const char* bitPointNames[] = {"Início", "Meio", "Fim"};
                constexpr int bitValuesForMenu[] = {0, 2, 3, 4, 6, 8, 12, 16};
                for (int point = 0; point < 3; ++point) {
                    juce::PopupMenu pointMenu;
                    for (int value = 0; value < 8; ++value)
                        pointMenu.addItem(1720 + point * 10 + value,
                            bitValuesForMenu[value] == 0 ? utf8("Padrão do preset") : juce::String(bitValuesForMenu[value]) + utf8(" bits"), true,
                            int(std::lround(bitcrushCurve.values[static_cast<size_t>(point)])) == bitValuesForMenu[value]);
                    bitcrushAutoMenu.addSubMenu(utf8(bitPointNames[point]) + " · " + juce::String(int(std::lround(bitcrushCurve.values[static_cast<size_t>(point)]))) + utf8(" bits"), pointMenu);
                }
                menu.addSubMenu(utf8("Bitcrush · automação: ") + (bitcrushCurve.enabled ? utf8("ligada") : utf8("desligada")), bitcrushAutoMenu);
                juce::PopupMenu cutoffMenu;
                cutoffMenu.addItem(400, utf8("Padrão do preset"), true, song.stemFilterCutoff[index] == 0);
                constexpr int cutoffs[] = {80,160,320,640,1200,2500,5000,8000,12000,16000};
                for (int i = 0; i < 10; ++i)
                    cutoffMenu.addItem(401 + i, juce::String(cutoffs[i]) + " Hz", true, song.stemFilterCutoff[index] == cutoffs[i]);
                menu.addSeparator();
                menu.addSubMenu(utf8("Filtro · corte: ") + (song.stemFilterCutoff[index] == 0 ? utf8("padrão do preset") :
                    juce::String(int(song.stemFilterCutoff[index])) + " Hz"), cutoffMenu);
                juce::PopupMenu filterAutoMenu;
                const auto& filterCurve = song.stemFilterAutomation[index];
                filterAutoMenu.addItem(1200, utf8("Desativar · conservar curva"), true, !filterCurve.enabled);
                filterAutoMenu.addItem(1201, utf8("Abrir · 80 → 1200 → 8000 Hz"));
                filterAutoMenu.addItem(1202, utf8("Fechar · 16000 → 5000 → 800 Hz"));
                filterAutoMenu.addItem(1203, utf8("Arco · 320 → 8000 → 320 Hz"));
                menu.addSubMenu(utf8("Filtro · automação: ") + (filterCurve.enabled ? utf8("ligada") : utf8("desligada")), filterAutoMenu);
                juce::PopupMenu delayAutoMenu;
                const auto& delayMixCurve = song.stemDelayMixAutomation[index];
                const auto& delayFeedbackCurve = song.stemDelayFeedbackAutomation[index];
                delayAutoMenu.addItem(1300, utf8("Desativar · conservar curvas"), true, !delayMixCurve.enabled && !delayFeedbackCurve.enabled);
                delayAutoMenu.addItem(1301, utf8("Entrada · mistura 0 → 30 → 60%"));
                delayAutoMenu.addItem(1302, utf8("Saída · mistura 60 → 30 → 0%"));
                delayAutoMenu.addItem(1303, utf8("Pulso · mistura 10 → 60 → 10%"));
                menu.addSubMenu(utf8("Delay · automação: ") + (delayMixCurve.enabled || delayFeedbackCurve.enabled ? utf8("ligada") : utf8("desligada")), delayAutoMenu);
                juce::PopupMenu reverbAutoMenu;
                const auto& reverbCurve = song.stemReverbMixAutomation[index];
                reverbAutoMenu.addItem(1400, utf8("Desativar · conservar curva"), true, !reverbCurve.enabled);
                reverbAutoMenu.addItem(1401, utf8("Entrada · 0 → 25 → 50%"));
                reverbAutoMenu.addItem(1402, utf8("Saída · 50 → 25 → 0%"));
                reverbAutoMenu.addItem(1403, utf8("Arco · 10 → 50 → 10%"));
                const char* reverbPointNames[] = {"Início", "Meio", "Fim"};
                for (int point = 0; point < 3; ++point) {
                    juce::PopupMenu pointMenu;
                    for (int percent = 0; percent <= 60; percent += 10)
                        pointMenu.addItem(1450 + point * 10 + percent / 10, juce::String(percent) + "%", true,
                            int(std::lround(reverbCurve.values[static_cast<size_t>(point)] * 100)) == percent);
                    reverbAutoMenu.addSubMenu(utf8(reverbPointNames[point]) + " · " + juce::String(int(std::lround(reverbCurve.values[static_cast<size_t>(point)] * 100))) + "%", pointMenu);
                }
                menu.addSubMenu(utf8("Reverb · automação: ") + (reverbCurve.enabled ? utf8("ligada") : utf8("desligada")), reverbAutoMenu);
                juce::PopupMenu reverbDecayAutoMenu;
                const auto& reverbDecayCurve = song.stemReverbDecayAutomation[index];
                reverbDecayAutoMenu.addItem(1500, utf8("Desativar · conservar curva"), true, !reverbDecayCurve.enabled);
                reverbDecayAutoMenu.addItem(1501, utf8("Entrada · 0,25 → 1 → 2 s"));
                reverbDecayAutoMenu.addItem(1502, utf8("Saída · 2 → 1 → 0,25 s"));
                reverbDecayAutoMenu.addItem(1503, utf8("Arco · 0,5 → 3 → 0,5 s"));
                menu.addSubMenu(utf8("Reverb · decaimento automático: ") + (reverbDecayCurve.enabled ? utf8("ligado") : utf8("desligado")), reverbDecayAutoMenu);
                juce::PopupMenu mixMenu, feedbackMenu;
                for (int percent = 0; percent <= 60; percent += 10)
                    mixMenu.addItem(100 + percent / 10, juce::String(percent) + "%", true,
                        int(std::lround(song.stemDelayMix[index] * 100)) == percent);
                for (int percent = 0; percent <= 60; percent += 5)
                    feedbackMenu.addItem(200 + percent / 5, juce::String(percent) + "%", true,
                        int(std::lround(song.stemDelayFeedback[index] * 100)) == percent);
                menu.addSeparator();
                menu.addSubMenu(utf8("Delay · mistura: ") + juce::String(int(std::lround(song.stemDelayMix[index] * 100))) + "%", mixMenu);
                menu.addSubMenu(utf8("Delay · realimentação: ") + juce::String(int(std::lround(song.stemDelayFeedback[index] * 100))) + "%", feedbackMenu);
                menu.addItem(300, utf8("Restaurar delay · mistura 30%, realimentação 35%"));
                juce::PopupMenu reverbMixMenu;
                for (int percent = 0; percent <= 60; percent += 5)
                    reverbMixMenu.addItem(500 + percent / 5, juce::String(percent) + "%", true,
                        int(std::lround(song.stemReverbMix[index] * 100)) == percent);
                menu.addSeparator();
                menu.addSubMenu(utf8("Reverb · mistura: ") + juce::String(int(std::lround(song.stemReverbMix[index] * 100))) + "%", reverbMixMenu);
                menu.addItem(600, utf8("Restaurar mistura do reverb · 25%"));
                juce::PopupMenu decayMenu;
                constexpr float decayValues[] = {0,0.25f,0.5f,0.75f,1,1.5f,2,3};
                for (int i = 0; i < 8; ++i)
                    decayMenu.addItem(700 + i, i == 0 ? utf8("Padrão do preset") : juce::String(decayValues[i], 2) + utf8(" s · aprox."),
                        true, song.stemReverbDecay[index] == decayValues[i]);
                menu.addSubMenu(utf8("Reverb · decaimento: ") + (song.stemReverbDecay[index] == 0 ? utf8("padrão do preset") :
                    juce::String(song.stemReverbDecay[index], 2) + utf8(" s · aprox.")), decayMenu);
                juce::PopupMenu autoMenu;
                const auto& curve = song.stemAutomation[index];
                autoMenu.addItem(800, utf8("Desativar · conservar curva"), true, !curve.enabled);
                autoMenu.addItem(805, utf8("Ativar curva guardada"), true, curve.enabled);
                autoMenu.addSeparator();
                autoMenu.addItem(801, utf8("Entrada · 0 → 50 → 100%"));
                autoMenu.addItem(802, utf8("Saída · 100 → 50 → 0%"));
                autoMenu.addItem(803, utf8("Arco · 25 → 100 → 25%"));
                autoMenu.addItem(804, utf8("Uniforme · 100%"));
                const char* pointNames[] = {"Início", "Meio", "Fim"};
                for (int point = 0; point < 3; ++point) {
                    juce::PopupMenu levels;
                    for (int level = 0; level <= 4; ++level)
                        levels.addItem(900 + point * 10 + level, juce::String(level * 25) + "%", true,
                            curve.levels[static_cast<size_t>(point)] == level * 0.25f);
                    autoMenu.addSubMenu(utf8(pointNames[point]) + " · " + juce::String(int(std::lround(curve.levels[static_cast<size_t>(point)] * 100))) + "%", levels);
                }
                menu.addSeparator();
                menu.addSubMenu(utf8("Volume · automação: ") + (curve.enabled ? utf8("ligada") : utf8("desligada")), autoMenu);
                juce::PopupMenu panAutoMenu;
                const auto& panCurve = song.stemPanAutomation[index];
                panAutoMenu.addItem(1000,utf8("Desativar · conservar curva"),true,!panCurve.enabled);
                panAutoMenu.addItem(1001,utf8("Ativar curva guardada"),true,panCurve.enabled);
                panAutoMenu.addSeparator();
                panAutoMenu.addItem(1002,"L → C → R");
                panAutoMenu.addItem(1003,"R → C → L");
                panAutoMenu.addItem(1004,"C → L → C");
                panAutoMenu.addItem(1005,"C → R → C");
                const char* panPointNames[] = {"Início","Meio","Fim"};
                for (int point = 0; point < 3; ++point) {
                    juce::PopupMenu positions;
                    const char* positionNames[] = {"L100","L50","C","R50","R100"};
                    for (int level = 0; level < 5; ++level)
                        positions.addItem(1010 + point*10 + level,positionNames[level],true,
                            panCurve.levels[static_cast<size_t>(point)] == (level-2)*0.5f);
                    panAutoMenu.addSubMenu(utf8(panPointNames[point]),positions);
                }
                menu.addSubMenu(utf8("Pan · automação: ") + (panCurve.enabled ? utf8("ligada") : utf8("desligada")),panAutoMenu);
                juce::PopupMenu fxAutoMenu;
                const auto& fxCurve = song.stemFxAutomation[index];
                fxAutoMenu.addItem(1100,utf8("Desativar · efeito sempre segue preset"),true,!fxCurve.enabled);
                fxAutoMenu.addItem(1101,utf8("Ativar padrão guardado"),true,fxCurve.enabled);
                fxAutoMenu.addSeparator();
                fxAutoMenu.addItem(1102,utf8("Sempre ligado"));
                fxAutoMenu.addItem(1103,utf8("Somente entrada"));
                fxAutoMenu.addItem(1104,utf8("Somente meio"));
                fxAutoMenu.addItem(1105,utf8("Somente final"));
                fxAutoMenu.addItem(1106,utf8("Entrada + final"));
                const char* fxPointNames[] = {"Início","Meio","Fim"};
                for (int point = 0; point < 3; ++point) {
                    juce::PopupMenu states;
                    states.addItem(1110 + point*10,utf8("Desligado"),true,!fxCurve.active[static_cast<size_t>(point)]);
                    states.addItem(1111 + point*10,utf8("Ligado"),true,fxCurve.active[static_cast<size_t>(point)]);
                    fxAutoMenu.addSubMenu(utf8(fxPointNames[point]),states);
                }
                menu.addSubMenu(utf8("FX · automação: ") + (fxCurve.enabled ? utf8("ligada") : utf8("desligada")),fxAutoMenu);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&stemFx[static_cast<size_t>(stem)]),
                    [this, stem](int choice) {
                        const auto index = static_cast<size_t>(stem);
                        if (choice >= 1300 && choice <= 1303) {
                            auto& mix = song.stemDelayMixAutomation[index];
                            auto& feedback = song.stemDelayFeedbackAutomation[index];
                            mix.enabled = feedback.enabled = choice != 1300;
                            if (choice == 1301) { mix.values = {{0.0f,0.3f,0.6f}}; feedback.values = {{0.1f,0.35f,0.5f}}; }
                            if (choice == 1302) { mix.values = {{0.6f,0.3f,0.0f}}; feedback.values = {{0.5f,0.35f,0.1f}}; }
                            if (choice == 1303) { mix.values = {{0.1f,0.6f,0.1f}}; feedback.values = {{0.2f,0.5f,0.2f}}; }
                            for (size_t p = 0; p < 3; ++p) {
                                stemDelayAutoState[index].mix[p].store(mix.values[p], std::memory_order_release);
                                stemDelayAutoState[index].feedback[p].store(feedback.values[p], std::memory_order_release);
                            }
                            stemDelayAutoState[index].mixEnabled.store(mix.enabled, std::memory_order_release);
                            stemDelayAutoState[index].feedbackEnabled.store(feedback.enabled, std::memory_order_release);
                            status.setText(utf8("Delay do stem ") + juce::String(stem + 1) +
                                (mix.enabled ? utf8(" · automação ligada: mistura e realimentação em três pontos.") :
                                               utf8(" · automação desligada; curvas guardadas.")), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 1400 && choice <= 1403) {
                            auto& curve = song.stemReverbMixAutomation[index];
                            curve.enabled = choice != 1400;
                            if (choice == 1401) curve.values = {{0.0f,0.25f,0.5f}};
                            if (choice == 1402) curve.values = {{0.5f,0.25f,0.0f}};
                            if (choice == 1403) curve.values = {{0.1f,0.5f,0.1f}};
                            for (size_t p = 0; p < 3; ++p) stemReverbAutoState[index].mix[p].store(curve.values[p], std::memory_order_release);
                            stemReverbAutoState[index].enabled.store(curve.enabled, std::memory_order_release);
                            status.setText(utf8("Reverb do stem ") + juce::String(stem + 1) +
                                (curve.enabled ? utf8(" · automação ligada em três pontos.") : utf8(" · automação desligada; curva guardada.")), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 1450 && choice <= 1476) {
                            const int point = (choice - 1450) / 10;
                            const int step = (choice - 1450) % 10;
                            if (point > 2 || step > 6) return;
                            auto& curve = song.stemReverbMixAutomation[index];
                            curve.values[static_cast<size_t>(point)] = float(step) * 0.1f;
                            curve.enabled = true;
                            stemReverbAutoState[index].mix[static_cast<size_t>(point)].store(curve.values[static_cast<size_t>(point)], std::memory_order_release);
                            stemReverbAutoState[index].enabled.store(true, std::memory_order_release);
                            status.setText(utf8("Reverb do stem ") + juce::String(stem + 1) + utf8(" · ponto editado; automação ligada."), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 1500 && choice <= 1503) {
                            auto& curve = song.stemReverbDecayAutomation[index];
                            curve.enabled = choice != 1500;
                            if (choice == 1501) curve.values = {{0.25f, 1.0f, 2.0f}};
                            if (choice == 1502) curve.values = {{2.0f, 1.0f, 0.25f}};
                            if (choice == 1503) curve.values = {{0.5f, 3.0f, 0.5f}};
                            for (size_t p = 0; p < 3; ++p) stemReverbAutoState[index].decay[p].store(curve.values[p], std::memory_order_release);
                            stemReverbAutoState[index].decayEnabled.store(curve.enabled, std::memory_order_release);
                            status.setText(utf8("Reverb do stem ") + juce::String(stem + 1) +
                                (curve.enabled ? utf8(" · decaimento automatizado em três pontos.") : utf8(" · automação de decaimento desligada; curva guardada.")), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 1600 && choice <= 1607) {
                            constexpr int bitValues[] = {0, 2, 3, 4, 6, 8, 12, 16};
                            song.stemBitcrushBits[index] = bitValues[choice - 1600];
                            stemEffects[index].setBitcrushBits(song.stemBitcrushBits[index]);
                            status.setText(utf8("Bitcrush do stem ") + juce::String(stem + 1) + utf8(" · ") +
                                (song.stemBitcrushBits[index] == 0 ? utf8("profundidade do preset") : juce::String(song.stemBitcrushBits[index]) + utf8(" bits")), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 1700 && choice <= 1703) {
                            auto& curve = song.stemBitcrushAutomation[index];
                            curve.enabled = choice != 1700;
                            if (choice == 1701) curve.values = {{4,8,16}};
                            if (choice == 1702) curve.values = {{16,8,4}};
                            if (choice == 1703) curve.values = {{4,16,4}};
                            for (size_t p = 0; p < 3; ++p) stemBitcrushAutoState[index].bits[p].store(curve.values[p], std::memory_order_release);
                            stemBitcrushAutoState[index].enabled.store(curve.enabled, std::memory_order_release);
                            status.setText(utf8("Bitcrush do stem ") + juce::String(stem + 1) +
                                (curve.enabled ? utf8(" · automação ligada em três pontos.") : utf8(" · automação desligada; curva guardada.")), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 1720 && choice <= 1747) {
                            constexpr int bitValuesForMenu[] = {0, 2, 3, 4, 6, 8, 12, 16};
                            const int point = (choice - 1720) / 10;
                            const int value = (choice - 1720) % 10;
                            if (point > 2 || value > 7) return;
                            auto& curve = song.stemBitcrushAutomation[index];
                            curve.values[static_cast<size_t>(point)] = float(bitValuesForMenu[value]);
                            curve.enabled = true;
                            stemBitcrushAutoState[index].bits[static_cast<size_t>(point)].store(curve.values[static_cast<size_t>(point)], std::memory_order_release);
                            stemBitcrushAutoState[index].enabled.store(true, std::memory_order_release);
                            status.setText(utf8("Bitcrush do stem ") + juce::String(stem + 1) + utf8(" · ponto editado; automação ligada."), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 1200 && choice <= 1203) {
                            auto& curve = song.stemFilterAutomation[index];
                            curve.enabled = choice != 1200;
                            if (choice == 1201) curve.values = {{80.0f, 1200.0f, 8000.0f}};
                            if (choice == 1202) curve.values = {{16000.0f, 5000.0f, 800.0f}};
                            if (choice == 1203) curve.values = {{320.0f, 8000.0f, 320.0f}};
                            for (size_t p = 0; p < 3; ++p)
                                stemFilterAutoState[index].values[p].store(curve.values[p], std::memory_order_release);
                            stemFilterAutoState[index].enabled.store(curve.enabled, std::memory_order_release);
                            status.setText(utf8("Filtro do stem ") + juce::String(stem + 1) +
                                (curve.enabled ? utf8(" · automação ligada: ") : utf8(" · automação desligada; curva guardada: ")) +
                                juce::String(int(curve.values[0])) + " / " + juce::String(int(curve.values[1])) + " / " +
                                juce::String(int(curve.values[2])) + utf8(" Hz · início/meio/fim da grade."), juce::dontSendNotification);
                            return;
                        }
                        if ((choice >= 1000 && choice <= 1005) ||
                            (choice >= 1010 && choice <= 1034 && (choice-1010)%10 <= 4)) {
                            auto& curve = song.stemPanAutomation[index];
                            if (choice == 1000) curve.enabled = false;
                            else {
                                curve.enabled = true;
                                if (choice == 1002) curve.levels = {{-1,0,1}};
                                if (choice == 1003) curve.levels = {{1,0,-1}};
                                if (choice == 1004) curve.levels = {{0,-1,0}};
                                if (choice == 1005) curve.levels = {{0,1,0}};
                                if (choice >= 1010) curve.levels[static_cast<size_t>((choice-1010)/10)] = float((choice-1010)%10-2)*0.5f;
                            }
                            for (size_t p = 0; p < 3; ++p) stemPanAutoState[index].levels[p].store(curve.levels[p],std::memory_order_release);
                            stemPanAutoState[index].enabled.store(curve.enabled,std::memory_order_release);
                            stemFx[index].setToggleState(song.stemEffect[index] > 0 || song.stemAutomation[index].enabled ||
                                curve.enabled || song.stemFxAutomation[index].enabled,juce::dontSendNotification);
                            status.setText(utf8("Pan do stem ") + juce::String(stem+1) +
                                (curve.enabled ? utf8(" · automação ligada") : utf8(" · automação desligada; curva guardada")) +
                                utf8(" · início/meio/fim da grade."),juce::dontSendNotification);
                            return;
                        }
                        if ((choice >= 1100 && choice <= 1106) ||
                            (choice >= 1110 && choice <= 1131 && (choice-1110)%10 <= 1)) {
                            auto& curve = song.stemFxAutomation[index];
                            if (choice == 1100) curve.enabled = false;
                            else {
                                curve.enabled = true;
                                if (choice == 1102) curve.active = {{true,true,true}};
                                if (choice == 1103) curve.active = {{true,false,false}};
                                if (choice == 1104) curve.active = {{false,true,false}};
                                if (choice == 1105) curve.active = {{false,false,true}};
                                if (choice == 1106) curve.active = {{true,false,true}};
                                if (choice >= 1110) curve.active[static_cast<size_t>((choice-1110)/10)] = (choice-1110)%10 == 1;
                            }
                            for (size_t p = 0; p < 3; ++p) stemFxAutoState[index].active[p].store(curve.active[p],std::memory_order_release);
                            stemFxAutoState[index].enabled.store(curve.enabled,std::memory_order_release);
                            stemFx[index].setToggleState(song.stemEffect[index] > 0 || song.stemAutomation[index].enabled ||
                                song.stemPanAutomation[index].enabled || curve.enabled,juce::dontSendNotification);
                            status.setText(utf8("FX do stem ") + juce::String(stem+1) +
                                (curve.enabled ? utf8(" · liga/desliga automatizado; escolha um preset audível") : utf8(" · automação desligada; preset sempre ativo")) +
                                utf8(" · transições em 25% e 75% da grade."),juce::dontSendNotification);
                            return;
                        }
                        if ((choice >= 800 && choice <= 805) ||
                            (choice >= 900 && choice <= 924 && (choice - 900) % 10 <= 4)) {
                            auto& curve = song.stemAutomation[index];
                            if (choice == 800) curve.enabled = false;
                            else {
                                curve.enabled = true;
                                if (choice == 801) curve.levels = {{0,0.5f,1}};
                                if (choice == 802) curve.levels = {{1,0.5f,0}};
                                if (choice == 803) curve.levels = {{0.25f,1,0.25f}};
                                if (choice == 804) curve.levels = {{1,1,1}};
                                if (choice >= 900) curve.levels[static_cast<size_t>((choice - 900) / 10)] = float((choice - 900) % 10) * 0.25f;
                            }
                            for (size_t p = 0; p < 3; ++p) stemAutoState[index].levels[p].store(curve.levels[p], std::memory_order_release);
                            stemAutoState[index].enabled.store(curve.enabled, std::memory_order_release);
                            stemFx[index].setToggleState(song.stemEffect[index] > 0 || curve.enabled ||
                                song.stemPanAutomation[index].enabled || song.stemFxAutomation[index].enabled,juce::dontSendNotification);
                            status.setText(utf8("Volume do stem ") + juce::String(stem + 1) + (curve.enabled ? utf8(" · automação ligada: ") : utf8(" · automação desligada; curva guardada: ")) +
                                juce::String(int(std::lround(curve.levels[0] * 100))) + " / " + juce::String(int(std::lround(curve.levels[1] * 100))) + " / " +
                                juce::String(int(std::lround(curve.levels[2] * 100))) + utf8("% · início/meio/fim da grade inteira, antes dos FX."), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 700 && choice <= 707) {
                            constexpr float values[] = {0,0.25f,0.5f,0.75f,1,1.5f,2,3};
                            song.stemReverbDecay[index] = values[choice - 700];
                            stemReverbDecayValue[index].store(song.stemReverbDecay[index], std::memory_order_release);
                            status.setText(utf8("Reverb do stem ") + juce::String(stem + 1) + utf8(" · decaimento ") +
                                (choice == 700 ? utf8("padrão curto/longo") : juce::String(values[choice - 700], 2) + utf8(" s aproximados")) +
                                utf8(" · ative Reverb para ouvir."), juce::dontSendNotification);
                            return;
                        }
                        if ((choice >= 500 && choice <= 512) || choice == 600) {
                            song.stemReverbMix[index] = choice == 600 ? 0.25f : float(choice - 500) * 0.05f;
                            stemReverbMixValue[index].store(song.stemReverbMix[index], std::memory_order_release);
                            status.setText(utf8("Reverb do stem ") + juce::String(stem + 1) + utf8(" · mistura ") +
                                juce::String(int(std::lround(song.stemReverbMix[index] * 100))) +
                                utf8("% · escolha Reverb curto ou longo para ouvir."), juce::dontSendNotification);
                            return;
                        }
                        if (choice >= 400 && choice <= 410) {
                            constexpr int cutoffs[] = {0,80,160,320,640,1200,2500,5000,8000,12000,16000};
                            song.stemFilterCutoff[index] = float(cutoffs[choice - 400]);
                            stemFilterCutoffValue[index].store(song.stemFilterCutoff[index], std::memory_order_release);
                            status.setText(utf8("Filtro do stem ") + juce::String(stem + 1) + utf8(" · corte ") +
                                (choice == 400 ? utf8("padrão do preset") : juce::String(cutoffs[choice - 400]) + " Hz") +
                                utf8(" · atua em Quente, Escuro, Grão e Lo-fi."), juce::dontSendNotification);
                            return;
                        }
                        if ((choice >= 100 && choice <= 106) || (choice >= 200 && choice <= 212) || choice == 300) {
                            if (choice >= 100 && choice <= 106) song.stemDelayMix[index] = float(choice - 100) * 0.1f;
                            if (choice >= 200 && choice <= 212) song.stemDelayFeedback[index] = float(choice - 200) * 0.05f;
                            if (choice == 300) { song.stemDelayMix[index] = 0.3f; song.stemDelayFeedback[index] = 0.35f; }
                            stemDelayMixValue[index].store(song.stemDelayMix[index], std::memory_order_release);
                            stemDelayFeedbackValue[index].store(song.stemDelayFeedback[index], std::memory_order_release);
                            status.setText(utf8("Delay do stem ") + juce::String(stem + 1) + utf8(" · mistura ") +
                                juce::String(int(std::lround(song.stemDelayMix[index] * 100))) + utf8("% · realimentação ") +
                                juce::String(int(std::lround(song.stemDelayFeedback[index] * 100))) +
                                utf8("% · escolha um preset Delay para ouvir."), juce::dontSendNotification);
                            return;
                        }
                        if (choice < 1 || choice > 10) return;
                        song.stemEffect[static_cast<size_t>(stem)] = choice - 1;
                        stemEffectValue[static_cast<size_t>(stem)].store(choice - 1, std::memory_order_release);
                        stemFx[static_cast<size_t>(stem)].setToggleState(choice > 1 || song.stemAutomation[index].enabled ||
                            song.stemPanAutomation[index].enabled || song.stemFxAutomation[index].enabled,juce::dontSendNotification);
                        status.setText(utf8("FX do stem ") + juce::String(stem + 1) +
                            utf8(" atualizado · transição suave; salvo no projeto e no WAV."), juce::dontSendNotification);
                    });
            };
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff56d6ad));
            slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff101b2d));
            slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffffc65c));
            slider.setRange(-1.0, 1.0, 0.01);
            slider.setValue(song.stemPan[static_cast<size_t>(stem)], juce::dontSendNotification);
            gain.setSliderStyle(juce::Slider::LinearHorizontal);
            gain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            gain.setRange(0.0, 1.5, 0.01);
            gain.setValue(song.stemGain[static_cast<size_t>(stem)], juce::dontSendNotification);
            gain.setTooltip(utf8("Volume do stem ") + juce::String(stem + 1));
            gain.onValueChange = [this, stem] {
                const float value = float(stemGain[static_cast<size_t>(stem)].getValue());
                song.stemGain[static_cast<size_t>(stem)] = value;
                stemGainValue[static_cast<size_t>(stem)].store(value, std::memory_order_release);
            };
            mute.setButtonText("M"); mute.setClickingTogglesState(true);
            mute.setTooltip(utf8("Silenciar stem ") + juce::String(stem + 1));
            mute.onClick = [this, stem] { stemMuted[static_cast<size_t>(stem)].store(stemMute[static_cast<size_t>(stem)].getToggleState()); };
            solo.setButtonText("S"); solo.setClickingTogglesState(true);
            solo.setTooltip(utf8("Solo do stem ") + juce::String(stem + 1));
            solo.onClick = [this, stem] { stemSoloed[static_cast<size_t>(stem)].store(stemSolo[static_cast<size_t>(stem)].getToggleState()); };
            slider.setDoubleClickReturnValue(true, 0.0);
            stemPanValue[static_cast<size_t>(stem)].store(
                song.stemPan[static_cast<size_t>(stem)], std::memory_order_relaxed);
            slider.setTooltip(utf8("Pan do stem ") + juce::String(stem + 1) + utf8(": esquerda, centro, direita."));
            slider.onValueChange = [this, stem] {
                const float value = float(stemPan[static_cast<size_t>(stem)].getValue());
                song.stemPan[static_cast<size_t>(stem)] = value;
                stemPanValue[static_cast<size_t>(stem)].store(value, std::memory_order_release);
                const auto text = std::abs(value) < 0.025f ? juce::String("C") :
                    juce::String(value < 0.0f ? "L" : "R") +
                    juce::String(int(std::round(std::abs(value) * 100.0f)));
                stemPanLabel[static_cast<size_t>(stem)].setText(text, juce::dontSendNotification);
            };
            label.setText("C", juce::dontSendNotification);
            label.setColour(juce::Label::textColourId, juce::Colour(0xffd4e5ee));
            label.setJustificationType(juce::Justification::centredLeft);
            label.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 10.0f, juce::Font::bold)));
        }
        seedSource.addItem(utf8("Número"), 1);
        seedSource.addItem("Texto", 2);
        seedSource.addItem("Desenho", 3);
        seedSource.addItem(utf8("Glitch: máscara"), 4);
        seedSource.addItem("Glitch: XOR", 5);
        seedSource.addItem("Glitch: wrap", 6);
        seedSource.setSelectedId(1);
        seedInput.setInputRestrictions(20, "0123456789");
        seedInput.setText(juce::String(100000000 + juce::Random::getSystemRandom().nextInt(900000000)),
                          juce::dontSendNotification);
        seedInput.setTooltip(utf8("Um seed de origem. Texto/Desenho usam seus dados; Bytebeat e Frases derivam variações em contadores separados."));
        seedInput.onTextChange = [this] {
            if (!manualGlitchMode) return;
            const auto text = seedInput.getText().toStdString();
            unsigned intensity = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), intensity);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return;
            const auto bounded = std::clamp(intensity, 1u, 100u);
            if (bounded != intensity)
                seedInput.setText(juce::String(bounded), juce::dontSendNotification);
        };
        seedSource.onChange = [this] { updateGlitchControls(); };
        updateGlitchControls();
        addAndMakeVisible(directMode);
        addAndMakeVisible(resetDirectButton);
        addAndMakeVisible(play);
        addAndMakeVisible(cross);
        addAndMakeVisible(clear);
        addAndMakeVisible(undoDrawingButton);
        addAndMakeVisible(redoDrawingButton);
        addAndMakeVisible(glyph);
        addAndMakeVisible(functionMode);
        addAndMakeVisible(loopMode);
        addAndMakeVisible(cellSpan);
        addAndMakeVisible(repetitions);
        addAndMakeVisible(cellName);
        addAndMakeVisible(storeCellButton);
        addAndMakeVisible(bankSelect);
        addAndMakeVisible(stemSelect);
        addAndMakeVisible(autoRouteButton);
        addAndMakeVisible(roleSelect);
        bankSelect.setTooltip(utf8("Banco de células: escolha, busque por nome/fórmula/origem/ID, filtre favoritas ou consulte a proveniência no fim do menu. Ouvir célula faz a pré-escuta."));
        stemSelect.addItem("Stem 1", 1);
        stemSelect.addItem("Stem 2", 2);
        stemSelect.addItem("Stem 3", 3);
        stemSelect.addItem(utf8("Stem 4 · Ritmo"), 4);
        stemSelect.addItem(utf8("Stem 5 · Textura"), 5);
        stemSelect.addSeparator();
        stemSelect.addItem(utf8("Sugerir posição"), 6);
        stemSelect.addItem(utf8("Equilibrar som"), 7);
        stemSelect.setSelectedId(1, juce::dontSendNotification);
        autoRouteButton.setButtonText(utf8("AUTO"));
        autoRouteButton.setTooltip(utf8("Clique: ligar/desligar AUTO. Clique direito: ajustar a força e a frequência das derivações. AUTO distribui vozes nos stems; desligado, leitura/gesto e canal seguem escolhas manuais."));
        autoRouteButton.setClickingTogglesState(true);
        autoRouteButton.setToggleState(true, juce::dontSendNotification);
        configureTogglePalette(autoRouteButton, juce::Colour(0xff354351), juce::Colour(0xff16776c));
        autoRouteButton.onClick = [this] {
            if (risco::validInscription(activeInput())) updateLiveDrawing(false);
            status.setText(autoRouteButton.getToggleState()
                ? utf8("AUTO ativo · novas derivações a cada volta; vozes nos stems correspondentes.")
                : utf8("AUTO pausado · leitura, gesto e canal seguem suas escolhas manuais."),
                juce::dontSendNotification);
        };
        autoRouteButton.onContextMenu = [this] {
            juce::PopupMenu strength, pace, menu;
            strength.addItem(101, utf8("Suave · 40%"), true, autoEvolutionStrength == 1);
            strength.addItem(102, utf8("Equilibrada · 70%"), true, autoEvolutionStrength == 2);
            strength.addItem(103, utf8("Intensa · 100%"), true, autoEvolutionStrength == 3);
            pace.addItem(201, utf8("A cada volta"), true, autoEvolutionEveryLoops == 1);
            pace.addItem(202, utf8("A cada 2 voltas"), true, autoEvolutionEveryLoops == 2);
            pace.addItem(204, utf8("A cada 4 voltas"), true, autoEvolutionEveryLoops == 4);
            menu.addSubMenu(utf8("Força da evolução"), strength);
            menu.addSubMenu(utf8("Frequência"), pace);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&autoRouteButton),
                [this](int choice) {
                    if (choice >= 101 && choice <= 103) {
                        autoEvolutionStrength = choice - 100;
                        if (risco::validInscription(activeInput())) updateLiveDrawing(false);
                        const char* names[] = {"", "suave", "equilibrada", "intensa"};
                        status.setText(utf8("AUTO · força ") + utf8(names[autoEvolutionStrength]) + utf8(".") ,
                                       juce::dontSendNotification);
                    } else if (choice == 201 || choice == 202 || choice == 204) {
                        autoEvolutionEveryLoops = choice - 200;
                        status.setText(utf8("AUTO · nova derivação a cada ") +
                            juce::String(autoEvolutionEveryLoops) +
                            (autoEvolutionEveryLoops == 1 ? utf8(" volta.") : utf8(" voltas.")),
                            juce::dontSendNotification);
                    }
                });
        };
        stemSelect.setTooltip(utf8("Escolha o canal de destino da próxima célula e de Auto/REC. Sugerir posição calcula tempo e canal pela grade e pelos papéis, sem inserir. Equilibrar som considera também densidade e atividade. Revise e clique Inserir célula."));
        stemSelect.onChange = [this] {
            if (stemSelect.getSelectedId() == 6 || stemSelect.getSelectedId() == 7) {
                const bool balanceSound = stemSelect.getSelectedId() == 7;
                const int index = selectedCellIndex();
                const auto span = cellSpan.getSelectedId() == 1 ? risco::CellSpan::beat :
                                  cellSpan.getSelectedId() == 4 ? risco::CellSpan::twoBars :
                                  cellSpan.getSelectedId() == 3 ? risco::CellSpan::fourBars :
                                  risco::CellSpan::bar;
                const int anchor = startBeat.getText().getIntValue();
                const auto suggestion = index < 0 ? risco::PlacementSuggestion{} :
                    risco::suggestPlacement(song, song.bank[static_cast<size_t>(index)].id,
                                            span, repetitions.getSelectedId(), anchor, balanceSound);
                if (suggestion.valid) {
                    pendingStem = suggestion.stem;
                    startBeat.setText(juce::String(suggestion.startBeat));
                    juce::String profileText = utf8("perfil sem eventos musicais disponíveis");
                    if (suggestion.profile.hasEvents) {
                        profileText = suggestion.profile.restLike
                            ? utf8("pausa provável")
                            : juce::String(suggestion.profile.eventsPerBar, 1) +
                              utf8(" eventos/compasso · registro MIDI médio ") +
                              juce::String(suggestion.profile.meanMidi, 0) +
                              utf8(" · atividade ") +
                              juce::String(int(std::lround(suggestion.profile.activeFraction * 100.0f))) + "%";
                    } else if (suggestion.profile.hasActivity) {
                        profileText = utf8("atividade estimada da onda ") +
                            juce::String(int(std::lround(suggestion.profile.activeFraction * 100.0f))) + "%";
                    }
                    if (suggestion.profile.hasLevel)
                        profileText += utf8(" · RMS aprox. ") +
                            juce::String(int(std::lround(suggestion.profile.rms * 100.0f))) + "%";
                    const auto profileSummary = suggestion.profile.restLike
                        ? utf8("pausa provável")
                        : suggestion.profile.hasEvents
                            ? juce::String(suggestion.profile.eventsPerBar, 1) + utf8(" eventos/compasso")
                            : suggestion.profile.hasActivity
                                ? utf8("onda ativa ") + juce::String(int(std::lround(
                                    suggestion.profile.activeFraction * 100.0f))) + "%"
                                : utf8("sem mapa de eventos");
                    status.setTooltip(utf8("Perfil da célula: ") + profileText +
                        (balanceSound
                            ? utf8(". Equilibrar som usa densidade, atividade e RMS disponível para reduzir sobreposição entre stems; o registro é informativo.")
                            : utf8(". Perfil informativo; use Equilibrar som para que densidade, atividade e RMS influenciem a posição.")));
                    status.setText(utf8("Sugestão: tempo ") + juce::String(suggestion.startBeat) +
                        utf8(" · STEM ") + juce::String(suggestion.stem + 1) +
                        (suggestion.stemRoleMatch ? utf8(" · função compatível") :
                         suggestion.sameRoleConcurrent ? utf8(" · mesmo papel simultâneo") :
                         suggestion.roleOnStem ? utf8(" · papel continua") :
                         suggestion.motifOnStem ? utf8(" · motivo continua") :
                         utf8(" · pista livre")) +
                        utf8(" · ") + juce::String(suggestion.concurrentStems) +
                        utf8(" simultâneos · ") + profileSummary + utf8(". Inserir confirma."),
                        juce::dontSendNotification);
                } else {
                    status.setText(utf8("Sem sugestão perto do tempo indicado; escolha uma célula ou ajuste a posição."),
                                   juce::dontSendNotification);
                }
                stemSelect.setSelectedId(pendingStem + 1, juce::dontSendNotification);
                return;
            }
            pendingStem = stemSelect.getSelectedId() - 1;
            if (pendingStem >= 0 && pendingStem < risco::Song::stemCount) {
                autoRouteButton.setToggleState(false, juce::dontSendNotification);
                if (liveInputPlayback.load(std::memory_order_acquire) != nullptr)
                    updateLiveDrawing(false);
                status.setText(utf8("Destino da próxima célula: STEM ") + juce::String(pendingStem + 1) +
                    utf8(". Auto: frases e REC seguem esta escolha."), juce::dontSendNotification);
            }
        };
        roleSelect.addItem("P: livre", 1);
        roleSelect.addItem("P: pulso", 2);
        roleSelect.addItem("P: baixo", 3);
        roleSelect.addItem("P: harmonia", 4);
        roleSelect.addItem("P: melodia", 5);
        roleSelect.addItem("P: textura", 6);
        roleSelect.setSelectedId(1, juce::dontSendNotification);
        roleSelect.setEnabled(false);
        roleSelect.setTooltip(utf8("Papel musical da célula selecionada no banco. É uma intenção para a sugestão de posição; não muda o som. Fica salvo na música."));
        roleSelect.onChange = [this] {
            const int index = selectedCellIndex();
            const int selected = roleSelect.getSelectedId();
            if (index < 0 || selected < 1 || selected > 6) return;
            auto& cell = song.bank[static_cast<size_t>(index)];
            const auto nextRole = static_cast<risco::CellRole>(selected - 1);
            if (cell.role == nextRole) return;
            if (cell.revision == std::numeric_limits<std::uint32_t>::max()) {
                roleSelect.setSelectedId(int(cell.role) + 1, juce::dontSendNotification);
                status.setText(utf8("Limite de revisões desta célula."), juce::dontSendNotification);
                return;
            }
            pushBankUndo(cell);
            cell.role = nextRole;
            ++cell.revision;
            status.setText(utf8("Papel da célula: ") + roleSelect.getText() +
                           utf8(". Use Sugerir posição para escolher tempo e stem."),
                           juce::dontSendNotification);
        };
        addAndMakeVisible(updateCellButton);
        addAndMakeVisible(startBeat);
        addAndMakeVisible(placeCellButton);
        addAndMakeVisible(previewCellButton);
        addAndMakeVisible(playSongButton);
        addAndMakeVisible(seedTrackButton);
        addAndMakeVisible(restartSongButton);
        addAndMakeVisible(loopRangeButton);
        addAndMakeVisible(zoomOutButton);
        addAndMakeVisible(zoomInButton);
        addAndMakeVisible(zoomFitButton);
        addAndMakeVisible(undoGridButton);
        addAndMakeVisible(saveButton);
        addAndMakeVisible(openButton);
        addAndMakeVisible(removeLastButton);
        cellSpan.addItem("1 tempo", 1);
        cellSpan.addItem("1 compasso", 2);
        cellSpan.addItem("2 compassos", 4);
        cellSpan.addItem("4 compassos", 3);
        cellSpan.setSelectedId(2);
        for (int count = 1; count <= 8; ++count)
            repetitions.addItem(juce::String(count) + (count == 1 ? " vez" : " vezes"), count);
        repetitions.setTooltip(utf8("Quantas vezes a célula se repete na grade ou na pré-escuta: 1 a 8."));
        repetitions.setSelectedId(2);
        cellName.setTextToShowWhenEmpty(utf8("Nome da célula"), juce::Colours::grey);
        cellName.setInputRestrictions(64);
        startBeat.setTextToShowWhenEmpty("Tempo inicial", juce::Colours::grey);
        startBeat.setInputRestrictions(4, "0123456789");
        startBeat.setText("0");
        addAndMakeVisible(status);
        for (auto* label : {&activityStatus, &generationStatus}) {
            addAndMakeVisible(*label);
            label->setColour(juce::Label::backgroundColourId, juce::Colour(0xff17283a));
            label->setColour(juce::Label::textColourId, juce::Colour(0xffd4e5ee));
            label->setBorderSize({0, 6, 0, 6});
        }
        addAndMakeVisible(albumMenuButton);
        albumMenuButton.setTooltip(utf8("Planeja as faixas ou renderiza os WAVs de um álbum já criado."));
        albumMenuButton.onClick = [this] {
            const bool busy = (albumJob && albumJob->isThreadRunning()) ||
                              (albumRenderJob && albumRenderJob->isThreadRunning());
            juce::PopupMenu menu;
            menu.addItem(1, uiText("Planejar e gerar projetos…", "Plan and generate projects…", "Planifier et générer les projets…", "Planificar y generar proyectos…"), !busy);
            menu.addItem(3, uiText("Retomar projetos…", "Resume projects…", "Reprendre les projets…", "Reanudar proyectos…"), !busy);
            menu.addItem(2, uiText("Renderizar WAVs…", "Render WAVs…", "Rendre les WAV…", "Renderizar WAV…"), !busy);
            juce::Component::SafePointer<MainComponent> safeThis(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&albumMenuButton),
                [safeThis](int choice) {
                    if (!safeThis) return;
                    if (choice == 1) safeThis->openAlbumDialog();
                    if (choice == 2) safeThis->openAlbumFolderChooser();
                    if (choice == 3) safeThis->openAlbumFolderChooser(true);
                });
        };
        play.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b8b79));
        play.setTooltip(utf8("Recomeça a escuta ao vivo. AUTO distribui as vozes pelos stems; desligado, usa o canal escolhido."));
        cross.setTooltip(utf8("Silencia/reabilita o último traço; ele continua visível e pode ser desfeito."));
        functionMode.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff65529a));
        functionMode.setTooltip(utf8("Ativa a fórmula de onda bytebeat aplicada abaixo."));
        loopMode.setClickingTogglesState(true);
        loopMode.setToggleState(false, juce::dontSendNotification);
        storeCellButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff496bb8));
        storeCellButton.setTooltip(utf8("Guarda no banco desta música. Use Salvar para conservar a performance ao fechar o app."));
        updateCellButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff496bb8));
        placeCellButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffa87526));
        placeCellButton.setTooltip(uiText("Com AUTO ligado, escolhe tempo e canal compatíveis. Clique numa área vazia para dar uma referência temporal; o Risco decide pista e posição final. Desligue AUTO para inserir no tempo e STEM escolhidos.", "With AUTO on, chooses a matching time and lane. Click an empty area to set a timing reference; Risco decides the final lane and position. Turn AUTO off to place at the selected time and stem.", "Avec AUTO, choisit un temps et une piste compatibles. Cliquez dans une case vide pour donner un repère temporel ; Risco décide de la piste et position finales. Désactivez AUTO pour utiliser le temps et le Stem choisis.", "Con AUTO, elige tiempo y canal compatibles. Haga clic en un área vacía para dar una referencia temporal; Risco decide la pista y posición final. Desactive AUTO para insertar en el tiempo y Stem elegidos."));
        previewCellButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b8b79));
        previewCellButton.setTooltip(utf8("Toca somente a célula escolhida no banco; se seu bloco está selecionado na grade, ouve o trecho recortado."));
        seedTrackButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffffc658));
        seedTrackButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff252432));
        seedTrackButton.setTooltip(utf8("Gera uma composição de 64 compassos. Depois de revisar e salvar, toca do início ao fim."));
        seedTrackButton.onClick = [this] { openSeedTrackDialog(); };
        for (auto* button : {&playSongButton, &previewCellButton, &play}) {
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff95e9c7));
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff142b26));
        }
        playSongButton.setTooltip(utf8("Toca a música que está na grade. Clique em Faixa completa para gerar uma música nova."));
        restartSongButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff496bb8));
        restartSongButton.setTooltip(utf8("Volta ao início da música; se um loop de trecho estiver ativo, reinicia no início dele."));
        loopRangeButton.setClickingTogglesState(true);
        loopRangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffa87526));
        loopRangeButton.setTooltip(utf8("Shift+arraste na grade para escolher um trecho; este botão liga ou desliga sua repetição."));
        zoomOutButton.setButtonText(utf8("−"));
        zoomOutButton.setTooltip(utf8("Mostra mais compassos na grade."));
        zoomOutButton.onClick = [this] { zoomGrid(1.25f); };
        zoomInButton.setButtonText("+");
        zoomInButton.setTooltip(utf8("Aumenta a escala de tempo e aproxima os comprimentos das células."));
        zoomInButton.onClick = [this] { zoomGrid(0.8f); };
        zoomFitButton.setButtonText("FIT");
        zoomFitButton.setTooltip(utf8("Mostra a música inteira na grade."));
        zoomFitButton.onClick = [this] {
            gridZoomBeats = gridTotalBeats();
            gridScrollBeat = 0.0;
            repaint();
        };
        undoGridButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff496bb8));
        undoGridButton.setTooltip(utf8("Desfaz a última edição da grade (Ctrl+Z / Cmd+Z). O banco permanece intacto."));
        undoGridButton.onClick = [this] { undoGridEdit(); };
        undoGridButton.setEnabled(false);
        saveButton.setTooltip(utf8("Salvar conserva o projeto editável. Escolha Formato WAV: PCM16 ou PCM24 / 48 kHz; exporte mix ou stems (até 10 min), com volume/pan, MASTER e proteção. Não inclui mute/solo nem onda personalizada ativa só na sessão."));
        openButton.setTooltip(utf8("Abre um arquivo de música e restaura o banco e a grade salvos."));
        removeLastButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffa87526));
        removeLastButton.setTooltip(utf8("Retira da grade o bloco selecionado; se nada estiver selecionado, retira o último."));
        functionMode.setClickingTogglesState(true);
        configureTogglePalette(functionMode, juce::Colour(0xff3b374e), juce::Colour(0xff7656bd));
        configureTogglePalette(loopMode, juce::Colour(0xff354351), juce::Colour(0xff16776c));
        configureTogglePalette(autoMode, juce::Colour(0xff3b374e), juce::Colour(0xff7656bd));
        configureTogglePalette(directMode, juce::Colour(0xff354351), juce::Colour(0xff16776c));
        configureTogglePalette(recButton, juce::Colour(0xff414957), juce::Colour(0xffb33e4c));
        configureTogglePalette(autoPhraseButton, juce::Colour(0xff414957), juce::Colour(0xff7656bd));
        configureTogglePalette(loopRangeButton, juce::Colour(0xff414957), juce::Colour(0xffa87526));
        functionMode.onClick = [this] {
            formulaEnabled.store(functionMode.getToggleState(), std::memory_order_release);
            if (functionMode.getToggleState()) timbreSelect.setSelectedId(1);
        };
        applyFormula.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff65529a));
        applyFormula.setTooltip(utf8("Compila a fórmula escrita. Com REC ON, também a guarda no banco e a coloca na grade. Sem REC, use Ouvir fórmula e depois Guardar célula + Inserir célula."));
        applyFormula.onClick = [this] {
            timbreSelect.setSelectedId(1);
            if (compileAndApplyFormula() && recButton.getToggleState()) appendFormulaToBank();
        };
        formulaToNotesButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff496bb8));
        formulaToNotesButton.setTooltip(utf8("Abre pré-escuta, subdivisão e swing da fórmula. Acompanhar/Guiar acorde também oferecem progressão e duração dos acordes. Guardar frase conserva escolhas e eventos na célula."));
        formulaToNotesButton.onClick = [this] {
            juce::PopupMenu menu, progressions, rhythm;
            menu.addItem(1, utf8("Ouvir com as escolhas atuais"));
            progressions.addItem(10, utf8("Retorno · I–IV–V–I"), true, formulaProgressionChoice == 0);
            progressions.addItem(11, utf8("Ciclo · I–vi–IV–V"), true, formulaProgressionChoice == 1);
            progressions.addItem(12, utf8("Pedal · tônica"), true, formulaProgressionChoice == 2);
            rhythm.addItem(20, utf8("Um compasso por acorde"), true, formulaHarmonicBarsChoice == 1);
            rhythm.addItem(21, utf8("Dois compassos por acorde"), true, formulaHarmonicBarsChoice == 2);
            if (formulaMotionSelect.getSelectedId() >= 6) {
                menu.addSubMenu(utf8("Progressão (graus da escala)"), progressions);
                menu.addSubMenu(utf8("Ritmo harmônico"), rhythm);
                const auto scale = risco::makeFormulaScale(static_cast<risco::FormulaScalePreset>(
                    std::clamp(formulaScaleSelect.getSelectedId() - 1, 0, 26)));
                for (int s = 0; s < 4; ++s) {
                    juce::PopupMenu section;
                    const int base = 300 + s * 20, root = formulaChordRootChoices[static_cast<size_t>(s)];
                    section.addItem(base, utf8("Automático · seguir progressão"), true, root == -1);
                    section.addItem(base + 1, utf8("Silêncio neste compasso"), true, root == -2);
                    for (int degree = 0; degree < scale.degreeCount; ++degree)
                        section.addItem(base + 2 + degree, utf8("Grau ") + juce::String(degree + 1), true, root == degree);
                    menu.addSubMenu(utf8("Harmonia do compasso ") + juce::String(s + 1), section);
                }
            }
            juce::PopupMenu swing;
            juce::PopupMenu floatbeat;
            floatbeat.addItem(100, utf8("Seno · 220 Hz"));
            floatbeat.addItem(101, utf8("Triangular · 220 Hz"));
            floatbeat.addItem(102, utf8("Modulação de fase · 220/110 Hz"));
            menu.addSubMenu(utf8("Floatbeat · carregar exemplo no editor"), floatbeat);
            juce::PopupMenu subdivision;
            static const int divisions[] = {0, 1, 2, 3, 4, 6};
            static const char* divisionNames[] = {"Original", "Semínimas", "Colcheias", "Tercinas de colcheias", "Semicolcheias", "Sextinas"};
            for (int i = 0; i < 6; ++i)
                subdivision.addItem(40 + i, utf8(divisionNames[i]), true, formulaSubdivisionChoice == divisions[i]);
            menu.addSubMenu(utf8("Subdivisão · por semínima"), subdivision);
            menu.addItem(50, utf8("Síncope · antecipar apoios internos"), true, formulaSyncopationChoice);
            juce::PopupMenu pulses;
            const int counts[] = {0, 2, 3, 5, 7};
            for (int i = 0; i < 5; ++i)
                pulses.addItem(60 + i, counts[i] == 0 ? utf8("Usar subdivisão / swing / síncope") :
                    juce::String(counts[i]) + utf8(" pulsos regulares por compasso"), true, formulaPulsesChoice == counts[i]);
            menu.addSubMenu(utf8("Polirritmia · combinar células em stems"), pulses);
            swing.addItem(30, "Reto (50:50)", true, formulaSwingChoice == 0.0);
            swing.addItem(31, "Leve (60:40)", true, formulaSwingChoice == 0.2);
            swing.addItem(32, "Marcado (70:30)", true, formulaSwingChoice == 0.4);
            menu.addSubMenu(utf8("Swing · pares binários; tercinas retas"), swing);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&formulaToNotesButton),
                [this](int choice) {
                    if (choice >= 300 && choice < 380) {
                        const int section = (choice - 300) / 20, action = (choice - 300) % 20;
                        if (action > 13) return;
                        formulaChordRootChoices[static_cast<size_t>(section)] = action == 0 ? -1 : action == 1 ? -2 : action - 2;
                        previewFormulaAsNotes(); return;
                    }
                    if (choice >= 100 && choice <= 102) {
                        const char* examples[] = {"f:sin(t*6.28318530718*220)*.4", "f:(abs((t*220%1)*2-1)*2-1)*.4",
                            "f:sin(t*6.28318530718*220+sin(t*6.28318530718*110)*3)*.4"};
                        formulaInput.setText(examples[choice - 100], juce::dontSendNotification);
                        status.setText(utf8("Floatbeat no editor · Compilar fórmula e Ouvir fórmula. t em segundos; saída -1 a 1, taxa 8 kHz."), juce::dontSendNotification);
                        return;
                    }
                    if (choice >= 10 && choice <= 12) formulaProgressionChoice = choice - 10;
                    else if (choice == 20 || choice == 21) formulaHarmonicBarsChoice = choice - 19;
                    else if (choice >= 30 && choice <= 32) formulaSwingChoice = (choice - 30) * 0.2;
                    else if (choice >= 40 && choice <= 45) {
                        static const int divisions[] = {0, 1, 2, 3, 4, 6};
                        formulaSubdivisionChoice = divisions[choice - 40];
                    }
                    else if (choice == 50) formulaSyncopationChoice = !formulaSyncopationChoice;
                    else if (choice >= 60 && choice <= 64) {
                        const int counts[] = {0, 2, 3, 5, 7};
                        formulaPulsesChoice = counts[choice - 60];
                    }
                    else if (choice != 1) return;
                    previewFormulaAsNotes();
                });
        };
        saveFormulaNotesButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffa87526));
        saveFormulaNotesButton.setTooltip(utf8("Guarda fórmula, escala, tônica e movimento como célula e acrescenta a frase ao fim da grade."));
        saveFormulaNotesButton.onClick = [this] { saveFormulaEventsToArrangement(); };
        formulaScaleSelect.addItem(utf8("Escala: maior"), 1);
        formulaScaleSelect.addItem(utf8("Escala: menor natural"), 2);
        formulaScaleSelect.addItem(utf8("Escala: dórico"), 3);
        formulaScaleSelect.addItem(utf8("Escala: pentatônica"), 4);
        formulaScaleSelect.addItem(utf8("Escala: menor harmônica"), 5);
        formulaScaleSelect.addItem(utf8("Escala: menor melódica"), 6);
        formulaScaleSelect.addItem(utf8("Escala: frígio"), 7);
        formulaScaleSelect.addItem(utf8("Escala: mixolídio"), 8);
        formulaScaleSelect.addItem(utf8("Escala: lídio"), 9);
        formulaScaleSelect.addItem(utf8("Escala: blues menor"), 10);
        formulaScaleSelect.addItem(utf8("Escala: tons inteiros"), 11);
        formulaScaleSelect.addItem(utf8("Escala: cromática"), 12);
        formulaScaleSelect.addSeparator();
        formulaScaleSelect.addItem(utf8("Jazz: bebop dom."), 13);
        formulaScaleSelect.addItem(utf8("Jazz: bebop maior"), 14);
        formulaScaleSelect.addItem(utf8("Jazz: alterada"), 15);
        formulaScaleSelect.addItem(utf8("Jazz: diminuta S–T"), 16);
        formulaScaleSelect.addItem(utf8("Jazz: diminuta T–S"), 17);
        formulaScaleSelect.addItem(utf8("Jazz: lídio dom."), 18);
        formulaScaleSelect.addSeparator();
        formulaScaleSelect.addItem(utf8("Klezmer: Freygish"), 19);
        formulaScaleSelect.addItem(utf8("Klezmer: Misheberakh"), 20);
        formulaScaleSelect.addSeparator();
        formulaScaleSelect.addItem(utf8("Cor: maior harm."), 21);
        formulaScaleSelect.addItem(utf8("Cor: menor napol."), 22);
        formulaScaleSelect.addItem(utf8("Cor: menor húng."), 23);
        formulaScaleSelect.addItem(utf8("Cor: dupla harm."), 24);
        formulaScaleSelect.addItem(utf8("Cor: aumentada"), 25);
        formulaScaleSelect.addItem(utf8("Cor: persa"), 26);
        formulaScaleSelect.addItem(utf8("Cor: Prometheus"), 27);
        formulaScaleSelect.setSelectedId(1, juce::dontSendNotification);
        formulaScaleSelect.setTooltip(utf8("Escolha entre 27 presets. O grupo Jazz reúne escalas usuais de improvisação; Klezmer oferece Freygish/Ahava Rabbah e Misheberakh. A máquina pode transformar o campo a cada compasso."));
        const char* tonicNames[] = {
            "Tom: Dó", "Tom: Dó♯/Ré♭", "Tom: Ré", "Tom: Ré♯/Mi♭",
            "Tom: Mi", "Tom: Fá", "Tom: Fá♯/Sol♭", "Tom: Sol",
            "Tom: Sol♯/Lá♭", "Tom: Lá", "Tom: Lá♯/Si♭", "Tom: Si"
        };
        for (int i = 0; i < 12; ++i) formulaTonicSelect.addItem(utf8(tonicNames[i]), i + 1);
        formulaTonicSelect.setSelectedId(1, juce::dontSendNotification);
        formulaTonicSelect.setTooltip(utf8("Tônica inicial da leitura Fórmula → notas. A escolha é salva em cada frase; afeta altura e transfiguração."));
        responseModeSelect.addItem(utf8("Resposta: desligada"), 1);
        const char* responseDirections[] = {"1 → 2", "2 → 3", "3 → 1"};
        const char* responseIntervals[] = {"4ª ↓", "4ª ↑", "8ª ↓"};
        for (int direction = 0; direction < 3; ++direction)
            for (int interval = 0; interval < 3; ++interval)
                responseModeSelect.addItem(utf8("Stem ") + responseDirections[direction] + " · " +
                    responseIntervals[interval], 2 + direction * 3 + interval);
        responseModeSelect.setSelectedId(song.interStemResponse + 1, juce::dontSendNotification);
        responseModeSelect.addSeparator();
        responseModeSelect.addSectionHeading(utf8("Critério de resposta"));
        responseModeSelect.addItem(utf8("Original · permite sobrepor"), 100);
        responseModeSelect.addItem(utf8("Só no espaço livre"), 101);
        responseModeSelect.addItem(utf8("Papel + registro + espaço"), 102);
        responseModeSelect.addItem(utf8("Harmonia + papel + espaço"), 103);
        responseModeSelect.addItem(utf8("Seguir mudanças harmônicas"), 104);
        responseModeSelect.addItem(utf8("Tensionar início → apoiar"), 105);
        responseModeSelect.addItem(utf8("Resolver último ataque"), 106);
        responseModeSelect.addItem(utf8("Pausas internas + harmonia"), 107);
        responseModeSelect.addSeparator();
        responseModeSelect.addSectionHeading(utf8("Seleção do motivo"));
        responseModeSelect.addItem(utf8("Motivo inteiro"), 200);
        responseModeSelect.addItem(utf8("Ataques estruturais · até 4"), 201);
        responseModeSelect.addItem(utf8("Contorno essencial · extremos"), 202);
        responseModeSelect.addSectionHeading(utf8("Densidade da resposta"));
        responseModeSelect.addItem("100%",210);
        responseModeSelect.addItem("50%",211);
        responseModeSelect.addItem("25%",212);
        responseModeSelect.addSeparator();
        responseModeSelect.addSectionHeading(utf8("Memória entre respostas"));
        responseModeSelect.addItem(utf8("Independentes"), 300);
        responseModeSelect.addItem(utf8("Evitar registro repetido"), 301);
        responseModeSelect.addItem(utf8("Ligar ao final anterior"), 302);
        const char* intentionNames[] = {"Automática", "Eco", "Contraste", "Tensão", "Resolução", "Silêncio"};
        for (int section = 0; section < 4; ++section) {
            juce::PopupMenu intentions;
            for (int intent = 0; intent < 6; ++intent)
                intentions.addItem(400 + section * 10 + intent, utf8(intentionNames[intent]), true,
                    song.responseSectionIntent[static_cast<size_t>(section)] == intent);
            responseModeSelect.getRootMenu()->addSubMenu(utf8("Intenção da seção ") + juce::String(section + 1), intentions);
        }
        responseModeSelect.setTooltip(utf8("Chamada e resposta: direção/intervalo, política, motivo, densidade, memória e intenção por seção. Memória pode evitar o mesmo registro ou ligar a entrada ao final anterior. Eco, contraste, tensão, resolução e silêncio são transformações explícitas; uma resposta por bloco, sem recursão."));
        responseModeSelect.onChange = [this] {
            const int choice = responseModeSelect.getSelectedId();
            if (choice >= 100 && choice <= 107) {
                song.responsePolicy = choice - 100;
                responseModeSelect.setSelectedId(song.interStemResponse + 1, juce::dontSendNotification);
            } else if (choice >= 200 && choice <= 202) {
                song.responseMotif = choice-200;
                responseModeSelect.setSelectedId(song.interStemResponse+1,juce::dontSendNotification);
            } else if (choice >= 210 && choice <= 212) {
                song.responseDensity = choice-210;
                responseModeSelect.setSelectedId(song.interStemResponse+1,juce::dontSendNotification);
            } else if (choice >= 300 && choice <= 302) {
                song.responseMemory = choice - 300;
                responseModeSelect.setSelectedId(song.interStemResponse+1,juce::dontSendNotification);
            } else if (choice >= 400 && choice <= 435) {
                const int section = (choice - 400) / 10, intent = (choice - 400) % 10;
                if (section < 0 || section >= 4 || intent < 0 || intent > 5) return;
                song.responseSectionIntent[static_cast<size_t>(section)] = intent;
                responseModeSelect.setSelectedId(song.interStemResponse+1,juce::dontSendNotification);
            } else song.interStemResponse = std::clamp(choice - 1, 0, 9);
            refreshPlaybackAfterGridEdit();
            status.setText(song.interStemResponse > 0
                ? utf8("Resposta ativa: ") + responseModeSelect.getText() + utf8(" · critério ") +
                  utf8(song.responsePolicy == 7 ? "pausas internas/papel/harmonia" : song.responsePolicy == 6 ? "resolver último ataque" : song.responsePolicy == 5 ? "tensão inicial/apoio" : song.responsePolicy == 4 ? "harmonia por ataque" : song.responsePolicy == 3 ? "harmonia/papel/espaço" : song.responsePolicy == 2 ? "papel/registro/espaço" : song.responsePolicy == 1 ? "espaço livre" : "original") +
                  utf8(" · motivo ") + utf8(song.responseMotif == 2 ? "contorno essencial" : song.responseMotif == 1 ? "ataques estruturais" : "inteiro") +
                  utf8(" · densidade ") + juce::String(song.responseDensity == 2 ? 25 : song.responseDensity == 1 ? 50 : 100) + "%" +
                  utf8(" · memória ") + utf8(song.responseMemory == 2 ? "ligar finais" : song.responseMemory == 1 ? "variar registro" : "independente")
                : utf8("Resposta entre stems desligada."), juce::dontSendNotification);
        };
        formulaMotionSelect.addItem("Seguir", 1);
        formulaMotionSelect.addItem("Contrapor", 2);
        formulaMotionSelect.addItem("Transfigurar", 3);
        formulaMotionSelect.addItem("Encadear", 4);
        formulaMotionSelect.addItem("Cadenciar", 5);
        formulaMotionSelect.addItem("Acompanhar", 6);
        formulaMotionSelect.addItem("Guiar acorde", 7);
        formulaMotionSelect.setSelectedId(1, juce::dontSendNotification);
        formulaMotionSelect.setTooltip(utf8("Seguir altera a escala; Contrapor espelha; Transfigurar interpola na família. Encadear aproxima oitavas; Cadenciar resolve na tônica. Acompanhar acrescenta baixo e duas vozes. Guiar acorde também aproxima a melodia das notas do acorde nos apoios do agrupamento métrico."));
        addAndMakeVisible(formulaGroupingSelect);
        formulaGroupingSelect.setTooltip(utf8("Agrupamento dos acentos em compassos ímpares. Automático mantém uma escolha estável derivada da fórmula; opções manuais ficam salvas na música."));
        formulaGroupingSelect.onChange = [this] {
            song.formulaMetricGrouping = formulaGroupingSelect.getSelectedId() <= 1
                ? -1 : formulaGroupingSelect.getSelectedId() - 2;
        };
        refreshMetricGroupingOptions();
        generateFormulaButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff65529a));
        generateFormulaButton.setTooltip(utf8("Cada fórmula gerada vai para o banco e para a grade; o repositório local de fórmulas persiste entre músicas. A música precisa ser salva em arquivo para conservar sua performance."));
        generateFormulaButton.onClick = [this] {
            if (seedSource.getSelectedId() >= 4) {
                applyManualGlitch();
                return;
            }
            autoMode.setToggleState(true, juce::dontSendNotification);
            startFormulaEvolution();
        };
        repositorySelect.setTextWhenNothingSelected(utf8("Repositório de funções"));
        repositorySelect.setTooltip(utf8("Biblioteca local persistente, independente do banco e do arquivo da música atual."));
        reuseFormulaButton.onClick = [this] { reuseSelectedFormula(); };
        importCatalogButton.onClick = [this] { chooseFormulaCatalog(); };
        varyPhraseButton.setTooltip(utf8("Escolha formas V2–V9 e gere a pré-escuta. V7 usa estado matemático; V8 cria arco com ápice; V9 desenvolve e retorna. Marque/desmarque ou edite seções, guarde takes e mantenha ao menos uma ativa. A receita acompanha a célula salva."));
        varyPhraseButton.onClick = [this] {
            juce::PopupMenu forms;
            forms.addItem(1, utf8("Variar forma atual"));
            forms.addSeparator();
            forms.addItem(2, utf8("A – A′ – contraste – retorno"), true,
                          selectedPhraseVersion == 2);
            forms.addItem(3, "A – B – A – C", true, selectedPhraseVersion == 3);
            forms.addItem(4, utf8("A – B · chamada–resposta (2 compassos)"), true,
                          selectedPhraseVersion == 4);
            forms.addItem(6, utf8("A – B – A – C · arco dinâmico"), true,
                          selectedPhraseVersion == 5);
            forms.addItem(7, utf8("V6 · frase nativa do compasso"), true, selectedPhraseVersion == 6);
            forms.addItem(8, utf8("V7 · mapa logístico quantizado"), true, selectedPhraseVersion == 7);
            forms.addItem(20, utf8("V8 · arco A–A′–ápice–retorno"), true, selectedPhraseVersion == 8);
            forms.addItem(21, utf8("V9 · espiral A–B–C–A′"), true, selectedPhraseVersion == 9);
            forms.addItem(9, utf8("Repetir variação atual · reset"), currentPhraseGenerated);
            forms.addSeparator();
            forms.addSectionHeader(utf8("Seções ativas · clique para editar"));
            const std::array<const char*, 4> sectionNames = selectedPhraseVersion == 8
                ? std::array<const char*, 4>{"A · exposição", "A′ · crescimento", "Ápice", "Retorno"}
                : selectedPhraseVersion == 9
                ? std::array<const char*, 4>{"A · exposição", "B · desvio", "C · desenvolvimento", "A′ · retorno"}
                : (selectedPhraseVersion == 3 || selectedPhraseVersion >= 5)
                ? std::array<const char*, 4>{"A · compasso 1", "B · compasso 2", "A · compasso 3", "C · compasso 4"}
                : selectedPhraseVersion == 4
                ? std::array<const char*, 4>{"A · compasso 1", "B · compasso 2", "(sem seção)", "(sem seção)"}
                : std::array<const char*, 4>{"A · compasso 1", "A′ · compasso 2", "Contraste · compasso 3", "Retorno · compasso 4"};
            for (unsigned section = 0; section < (selectedPhraseVersion == 4 ? 2u : 4u); ++section)
                forms.addItem(10 + int(section), utf8(sectionNames[section]), currentPhraseGenerated,
                              (currentPhraseSectionMask & (1u << section)) != 0);
            forms.addSeparator();
            forms.addItem(5, utf8("Ativar todas as seções"), currentPhraseGenerated);
            for (int s = 0; s < (currentPhraseVersion == 4 ? 2 : 4); ++s) {
                juce::PopupMenu edit;
                const auto& values = currentSectionEdits[static_cast<size_t>(s)];
                const int base = 100 + s * 20;
                edit.addSectionHeader(utf8("Densidade · mantém os tempos restantes"));
                edit.addItem(base, "100%", true, values.density == 1.0f);
                edit.addItem(base + 1, "50%", true, values.density == 0.5f);
                edit.addItem(base + 2, "25%", true, values.density == 0.25f);
                edit.addSectionHeader(utf8("Registro"));
                edit.addItem(base + 3, utf8("Uma oitava abaixo"), true, values.octaves == -1);
                edit.addItem(base + 4, "Original", true, values.octaves == 0);
                edit.addItem(base + 5, utf8("Uma oitava acima"), true, values.octaves == 1);
                edit.addSectionHeader(utf8("Intensidade"));
                edit.addItem(base + 6, "60%", true, values.gain == 0.6f);
                edit.addItem(base + 7, "100%", true, values.gain == 1.0f);
                edit.addItem(base + 8, "125%", true, values.gain == 1.25f);
                edit.addItem(base + 9, utf8("Restaurar seção"));
                juce::PopupMenu meters;
                meters.addItem(base + 10, utf8("Seguir compasso da música"), true, currentPhraseMeters[static_cast<size_t>(s)] == 0);
                for (int m = 2; m <= 8; ++m)
                    meters.addItem(base + 9 + m, m == 8 ? juce::String("6/8") : juce::String(m) + "/4", true,
                        currentPhraseMeters[static_cast<size_t>(s)] == m);
                edit.addSubMenu(utf8("Compasso da seção · V6/V7"), meters, currentPhraseVersion >= 6);
                juce::PopupMenu lengths;
                for (int bars = 1; bars <= 4; ++bars)
                    lengths.addItem(1000 + s * 10 + bars,
                        juce::String(bars) + (bars == 1 ? utf8(" compasso") : utf8(" compassos")), true,
                        currentPhraseSectionBars[static_cast<size_t>(s)] == bars);
                edit.addSubMenu(utf8("Duração da seção · motivo estendido"), lengths, currentPhraseVersion >= 6);
                forms.addSubMenu(utf8("Editar ") + utf8(sectionNames[static_cast<size_t>(s)]), edit, currentPhraseGenerated);
            }
            forms.addItem(200, utf8("Comparar · original sem edições"), currentPhraseGenerated, compareOriginalSections);
            forms.addSeparator();
            forms.addItem(201, utf8("Guardar prévia como novo take"), currentPhraseGenerated && song.cellCount < int(song.bank.size()));
            juce::PopupMenu takes;
            takes.addSectionHeader(utf8("Guarde a prévia antes de trocar"));
            std::array<std::uint32_t,64> takeIds{};
            int takeCount = 0;
            for (int i = 0; i < song.cellCount; ++i) {
                const auto& cell = song.bank[static_cast<size_t>(i)];
                if (!currentPhraseGenerated || !cell.generatedPhrase || cell.phraseSeed != currentPhraseSeed) continue;
                takeIds[static_cast<size_t>(takeCount)] = cell.id;
                takes.addItem(500 + takeCount, juce::String::fromUTF8(cell.name.c_str()) + " #" + juce::String(cell.id),
                    true, song.preferredTakeId == cell.id);
                ++takeCount;
            }
            forms.addSubMenu(utf8("Ouvir e preferir take desta seed"), takes, takeCount > 0);
            forms.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&varyPhraseButton),
                [this,takeIds](int choice) {
                    if (choice == 201) { storeCurrentPhraseTake(); return; }
                    if (choice >= 500 && choice < 564) {
                        const auto id = takeIds[static_cast<size_t>(choice - 500)];
                        if (id == 0) return;
                        for (int i = 0; i < song.cellCount; ++i) if (song.bank[static_cast<size_t>(i)].id == id) {
                            song.preferredTakeId = id;
                            bankQuery.clear(); bankFavoritesOnly = false;
                            selectedPlacement = -1; // Compare the entire take, not a selected crop.
                            autoPhraseButton.setToggleState(false,juce::dontSendNotification);
                            refreshBank(i);
                            cellSpan.setSelectedId(currentPhraseVersion == 4 ? 4 : 3,juce::dontSendNotification);
                            repetitions.setSelectedId(1,juce::dontSendNotification);
                            const auto before = songSnapshots.size();
                            previewSelectedCell();
                            if (songSnapshots.size() > before)
                                status.setText(utf8("Take preferido #") + juce::String(id) +
                                    utf8(" · pré-escuta integral; grade preservada. Salvar conserva a escolha."),juce::dontSendNotification);
                            break;
                        }
                        return;
                    }
                    if (choice == 9 && currentPhraseGenerated) {
                        generatePhraseFromSeed(currentPhraseVariation, true); return;
                    }
                    if (choice == 200) {
                        compareOriginalSections = !compareOriginalSections;
                        generatePhraseFromSeed(currentPhraseVariation);
                        return;
                    }
                    if (choice >= 1001 && choice <= 1034) {
                        const int section = (choice - 1000) / 10;
                        const int bars = (choice - 1000) % 10;
                        if (section < 0 || section >= 4 || bars < 1 || bars > 4 || currentPhraseVersion < 6) return;
                        currentPhraseSectionBars[static_cast<size_t>(section)] = bars;
                        compareOriginalSections = false;
                        generatePhraseFromSeed(currentPhraseVariation);
                        return;
                    }
                    if (choice >= 100 && choice < 180) {
                        const int section = (choice - 100) / 20, action = (choice - 100) % 20;
                        auto& edit = currentSectionEdits[static_cast<size_t>(section)];
                        if (action <= 2) edit.density = action == 0 ? 1.0f : action == 1 ? 0.5f : 0.25f;
                        else if (action <= 5) edit.octaves = action - 4;
                        else if (action <= 8) edit.gain = action == 6 ? 0.6f : action == 7 ? 1.0f : 1.25f;
                        else if (action == 9) { edit = {}; currentPhraseMeters[static_cast<size_t>(section)] = 0; currentPhraseSectionBars[static_cast<size_t>(section)] = 1; }
                        else if (action >= 10 && action <= 17 && currentPhraseVersion >= 6)
                            currentPhraseMeters[static_cast<size_t>(section)] = action == 10 ? 0 : action - 9;
                        else return;
                        compareOriginalSections = false;
                        generatePhraseFromSeed(currentPhraseVariation);
                        return;
                    }
                    if (choice == 5) {
                        currentPhraseSectionMask = currentPhraseVersion == 4 ? 0x03 : 0x0f;
                        generatePhraseFromSeed(currentPhraseVariation);
                        return;
                    }
                    if (choice >= 10 && choice <= 13) {
                        const unsigned validSections = currentPhraseVersion == 4 ? 0x03u : 0x0fu;
                        currentPhraseSectionMask = std::uint8_t(
                            (currentPhraseSectionMask ^ (1u << unsigned(choice - 10))) & validSections);
                        if (currentPhraseSectionMask == 0)
                            currentPhraseSectionMask = std::uint8_t(1u << unsigned(choice - 10));
                        if (currentPhraseGenerated)
                            generatePhraseFromSeed(currentPhraseVariation);
                        return;
                    }
                    unsigned requestedVersion = currentPhraseVersion;
                    if (choice == 2) requestedVersion = 2;
                    else if (choice == 3) requestedVersion = 3;
                    else if (choice == 4) requestedVersion = 4;
                    else if (choice == 6) requestedVersion = 5;
                    else if (choice == 7) requestedVersion = 6;
                    else if (choice == 8) requestedVersion = 7;
                    else if (choice == 20) requestedVersion = 8;
                    else if (choice == 21) requestedVersion = 9;
                    else if (choice != 1) return;
                    if (requestedVersion != currentPhraseVersion) {
                        selectedPhraseVersion = currentPhraseVersion = requestedVersion;
                        currentPhraseSectionMask = 0x0f;
                        currentSectionEdits = {};
                        currentPhraseMeters = {};
                        currentPhraseSectionBars = {{1,1,1,1}};
                        compareOriginalSections = false;
                    }
                    const auto variation = phraseVariation++;
                    if (generatePhraseFromSeed(variation) && recButton.getToggleState())
                        appendGeneratedPhrase();
                });
        };
        recButton.setClickingTogglesState(true);
        recButton.setTooltip(utf8("Registra cada nova função no banco e acrescenta à grade da performance."));
        recButton.onClick = [this] {
            status.setText(recButton.getToggleState()
                ? utf8("REC ativo — próximas funções entram no banco e na grade.")
                : utf8("REC pausado — material já gravado permanece na performance."),
                juce::dontSendNotification);
        };
        autoPhraseButton.setTooltip(utf8("Gera frases sucessivas na forma selecionada em Variar frase e as insere automaticamente na música. Desative para parar."));
        autoPhraseButton.setClickingTogglesState(true);
        autoPhraseButton.onClick = [this] {
            autoMode.setToggleState(false, juce::dontSendNotification);
            if (autoPhraseButton.getToggleState()) {
                phraseVariation = 0;
                if (generatePhraseFromSeed(phraseVariation++) && appendGeneratedPhrase()) {
                    nextPhraseMillis = juce::Time::getMillisecondCounter() + phraseCycleMillis();
                    showActivity(utf8("Frases auto · primeira frase inserida na grade"));
                } else {
                    autoPhraseButton.setToggleState(false, juce::dontSendNotification);
                    showActivity(status.getText());
                }
            } else {
                showActivity(utf8("Frases auto desligadas · grade preservada"));
            }
        };
        autoMode.setTooltip(utf8("Ouve e gera sucessivas fórmulas sonoras. Para uma composição do início ao fim, use Faixa completa."));
        autoMode.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff65529a));
        autoMode.setClickingTogglesState(true);
        autoMode.onClick = [this] {
            if (autoMode.getToggleState()) {
                if (startFormulaEvolution())
                    showActivity(utf8("Fórmula auto · ouvindo a primeira fórmula"));
                else
                    showActivity(status.getText());
            } else {
                showActivity(utf8("Fórmula auto desligada"));
            }
        };
        compileAndApplyFormula();
        directMode.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b8b79));
        directMode.setTooltip(utf8("Pré-escuta Bytebeat ao vivo, fora da música. Desative para parar essa pré-escuta."));
        directMode.setClickingTogglesState(true);
        directMode.onClick = [this] {
            bool enabled = directMode.getToggleState();
            if (enabled && (normalizeFormulaText(formulaInput.getText().toStdString()) !=
                            normalizeFormulaText(compiledFormulaExpression) ||
                            activeFormula.load(std::memory_order_acquire) == nullptr)) {
                if (!compileAndApplyFormula()) {
                    directMode.setToggleState(false, juce::dontSendNotification);
                    refreshToggleDisplays();
                    repaint();
                    return;
                }
                enabled = true;
            }
            directEnabled.store(enabled, std::memory_order_release);
            if (enabled) {
                resetDirect.store(true, std::memory_order_release);
                nextAutoTick = phraseCycleTicks();
                playing.store(true, std::memory_order_release);
                status.setText(compiledFormulaExpression.rfind("f:", 0) == 0
                    ? utf8("Floatbeat direto · t em segundos, fonte 8 kHz, saída -1 a 1.")
                    : utf8("Bytebeat direto: t avança globalmente a 8 kHz."),
                               juce::dontSendNotification);
            } else {
                autoMode.setToggleState(false, juce::dontSendNotification);
                restart.store(true, std::memory_order_release);
                playing.store(active.load(std::memory_order_acquire) != nullptr ||
                              activeSong.load(std::memory_order_acquire) != nullptr,
                              std::memory_order_release);
                status.setText(utf8("Leitura por notas retomada."), juce::dontSendNotification);
            }
        };
        resetDirectButton.onClick = [this] {
            resetDirect.store(true, std::memory_order_release);
            nextAutoTick = phraseCycleTicks();
            status.setText(utf8("Contador t reiniciado."), juce::dontSendNotification);
        };
        loopMode.onClick = [this] {
            const bool enabled = loopMode.getToggleState();
            loopEnabled.store(enabled, std::memory_order_release);
            if (enabled && active.load(std::memory_order_acquire) != nullptr)
                playing.store(true, std::memory_order_release);
        };
        loopRangeButton.onClick = [this] {
            const bool enabled = loopRangeButton.getToggleState() && loopRangeEndBeat > loopRangeStartBeat;
            loopRangeButton.setToggleState(enabled, juce::dontSendNotification);
            loopRangeEnabled.store(enabled, std::memory_order_release);
            status.setText(enabled
                ? utf8("Loop de trecho ativo; Shift+arraste na grade para mudar os limites.")
                : utf8("Loop de trecho desligado; a repetição geral continua disponível."),
                juce::dontSendNotification);
        };
        glyph.setTooltip(utf8("Escolha como texto/desenho organiza ritmo, intensidade ou altura. A curva autoral pode mover ataques, modelar intensidade ou criar um arco de altura."));
        glyph.onClick = [this] {
            juce::PopupMenu menu;
            menu.addItem(1, utf8("Glifo da fonte · primeiro caractere"));
            menu.addSectionHeader(utf8("Interpretação de texto/desenho"));
            menu.addItem(2, utf8("Editar curva autoral"), true,
                (inputInterpretationChoice >= 8 && inputInterpretationChoice <= 13) && inputGestureCurve.enabled);
            menu.addItem(3, utf8("Gesto autoral · intensidade crescente"), true,
                inputInterpretationChoice == 9 && inputGestureCurve.enabled);
            menu.addItem(4, utf8("Gesto autoral · intensidade decrescente"), true,
                inputInterpretationChoice == 10 && inputGestureCurve.enabled);
            menu.addItem(5,utf8("Gesto autoral · altura crescente"),true,
                inputInterpretationChoice == 11 && inputGestureCurve.enabled);
            menu.addItem(6,utf8("Gesto autoral · altura decrescente"),true,
                inputInterpretationChoice == 12 && inputGestureCurve.enabled);
            menu.addItem(7,utf8("Gesto autoral · altura em arco"),true,
                inputInterpretationChoice == 13 && inputGestureCurve.enabled);
            const char* names[] = {"Original", "Contorno de alturas · pulso regular", "Ritmo · nota fixa", "Intensidade · nota fixa e pulso regular", "Tempo invertido", "Gesto temporal · acelerar", "Gesto temporal · desacelerar", "Gesto temporal · respirar"};
            for (int i = 0; i <= 7; ++i) menu.addItem(10 + i, utf8(names[i]), true, inputInterpretationChoice == i);
            juce::PopupMenu strengthMenu;
            for (int step = 0; step <= 4; ++step)
                strengthMenu.addItem(100 + step, juce::String(step * 25) + (step == 0 ? utf8("% · original") : "%"),
                    true, inputInterpretationStrength == step * 0.25f);
            menu.addSubMenu(utf8("Força da leitura: ") + juce::String(int(std::lround(inputInterpretationStrength * 100))) + "%", strengthMenu);
            juce::PopupMenu rangeMenu;
            for (const int semitones : {6,12,24})
                rangeMenu.addItem(300 + semitones,juce::String(semitones) + utf8(" semitons"),true,
                    inputGestureRangeSemitones == semitones);
            menu.addSubMenu(utf8("Alcance de altura: ") + juce::String(inputGestureRangeSemitones) + utf8(" semitons"),rangeMenu);
            menu.addItem(200, utf8("Restaurar leitura completa · 100%"));
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&glyph), [this](int choice) {
                if (choice != 0)
                    autoRouteButton.setToggleState(false, juce::dontSendNotification);
                if (choice == 1) { loadGlyph(); return; }
                if (choice == 2) {
                    if (inputInterpretationChoice < 9 || inputInterpretationChoice > 13) inputInterpretationChoice = 8;
                    inputGestureCurve.enabled = true;
                    inputGestureCurve.values[0] = 0.0f;
                    inputGestureCurve.values[2] = 1.0f;
                    gestureEditor.setValues(inputGestureCurve.values);
                    gestureEditor.setVisible(true);
                    gestureEditor.toFront(true);
                    gestureEditor.grabKeyboardFocus();
                    updateLiveDrawing(true);
                    status.setText(utf8(inputInterpretationChoice == 9
                        ? "Curva de intensidade autoral ativa; arraste o ponto do meio para modelar o crescendo."
                        : inputInterpretationChoice == 10
                        ? "Curva de intensidade autoral ativa; arraste o ponto do meio para modelar o decrescendo."
                        : inputInterpretationChoice == 13
                        ? "Curva de altura em arco ativa; o contorno sobe e retorna à afinação de origem."
                        : inputInterpretationChoice >= 11
                        ? "Curva de altura autoral ativa; arraste o ponto do meio para modelar o percurso."
                        : "Gesto temporal autoral ativo; arraste o ponto do meio para editar a curva."), juce::dontSendNotification);
                    return;
                }
                if (choice == 3) {
                    inputInterpretationChoice = 9;
                    inputGestureCurve.enabled = true;
                    gestureEditor.setValues(inputGestureCurve.values);
                    gestureEditor.setVisible(true);
                    gestureEditor.toFront(true);
                    gestureEditor.grabKeyboardFocus();
                    updateLiveDrawing(true);
                    status.setText(utf8("Gesto autoral de intensidade crescente; a curva modela o ganho dos ataques."), juce::dontSendNotification);
                    return;
                }
                if (choice == 4) {
                    inputInterpretationChoice = 10;
                    inputGestureCurve.enabled = true;
                    gestureEditor.setValues(inputGestureCurve.values);
                    gestureEditor.setVisible(true);
                    gestureEditor.toFront(true);
                    gestureEditor.grabKeyboardFocus();
                    updateLiveDrawing(true);
                    status.setText(utf8("Gesto autoral de intensidade decrescente; a curva modela o ganho dos ataques."), juce::dontSendNotification);
                    return;
                }
                if (choice == 5 || choice == 6) {
                    inputInterpretationChoice = choice == 5 ? 11 : 12;
                    inputGestureCurve.enabled = true;
                    gestureEditor.setValues(inputGestureCurve.values);
                    gestureEditor.setVisible(true);
                    gestureEditor.toFront(true);
                    gestureEditor.grabKeyboardFocus();
                    updateLiveDrawing(true);
                    status.setText(choice == 5
                        ? utf8("Gesto autoral de altura crescente; alcance atual: ") + juce::String(inputGestureRangeSemitones) + utf8(" semitons.")
                        : utf8("Gesto autoral de altura decrescente; alcance atual: ") + juce::String(inputGestureRangeSemitones) + utf8(" semitons."),
                        juce::dontSendNotification);
                    return;
                }
                if (choice == 7) {
                    inputInterpretationChoice = 13;
                    inputGestureCurve.enabled = true;
                    gestureEditor.setValues(inputGestureCurve.values);
                    gestureEditor.setVisible(true);
                    gestureEditor.toFront(true);
                    gestureEditor.grabKeyboardFocus();
                    updateLiveDrawing(true);
                    status.setText(utf8("Gesto autoral de altura em arco; alcance atual: ") +
                        juce::String(inputGestureRangeSemitones) + utf8(" semitons. O contorno retorna à afinação de origem."),
                        juce::dontSendNotification);
                    return;
                }
                if (choice >= 306 && choice <= 324 && (choice == 306 || choice == 312 || choice == 324)) {
                    inputGestureRangeSemitones = choice - 300;
                    updateLiveDrawing(true);
                    status.setText(utf8("Alcance de altura do gesto: ") + juce::String(inputGestureRangeSemitones) + utf8(" semitons."),
                        juce::dontSendNotification);
                    return;
                }
                if (choice >= 10 && choice <= 17) inputInterpretationChoice = choice - 10;
                else if (choice >= 100 && choice <= 104) inputInterpretationStrength = float(choice - 100) * 0.25f;
                else if (choice == 200) inputInterpretationStrength = 1;
                else return;
                if (inputInterpretationChoice < 8 || inputInterpretationChoice > 13) inputGestureCurve.enabled = false;
                if (currentPhraseGenerated) currentPhraseGenerated = false;
                updateLiveDrawing(true);
                status.setText(utf8("Força da leitura ") + juce::String(int(std::lround(inputInterpretationStrength * 100))) +
                    utf8("% · 0% compara com o original; Guardar célula conserva origem e ajuste. Fórmulas não são afetadas."), juce::dontSendNotification);
            });
        };
        status.setColour(juce::Label::backgroundColourId, juce::Colour(0xff17283a));
        status.setColour(juce::Label::textColourId, juce::Colour(0xffe0f6ff));
        status.setBorderSize({0, 6, 0, 6});
        status.setText(uiText("Escreva ou desenhe: o som acompanha a entrada. AUTO desenvolve cinco camadas.", "Type or draw: sound follows your input. AUTO develops five layers.", "Écrivez ou dessinez : le son suit votre geste. AUTO développe cinq couches.", "Escriba o dibuje: el sonido sigue la entrada. AUTO desarrolla cinco capas."),
                       juce::dontSendNotification);
        play.onClick = [this] { playInscription(); };
        play.setTooltip(uiText("Recomeça a escuta ao vivo do texto ou desenho atual.", "Restart live listening for the current text or drawing.", "Relance l’écoute en direct du texte ou dessin actuel.", "Reinicia la escucha en vivo del texto o dibujo actual."));
        storeCellButton.onClick = [this] { storeCurrentCell(); };
        updateCellButton.setTooltip(utf8("Atualizar altera todas as ocorrências da célula. Abra para atualizar, desfazer ou refazer edições do banco (até 8 etapas nesta sessão)."));
        updateCellButton.onClick = [this] {
            juce::PopupMenu menu;
            menu.addItem(1, utf8("Atualizar célula em todas as ocorrências"), selectedCellIndex() >= 0);
            menu.addSeparator();
            menu.addItem(2, utf8("Desfazer edição do banco"), !bankUndo.empty());
            menu.addItem(3, utf8("Refazer edição do banco"), !bankRedo.empty());
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&updateCellButton),
                [this](int choice) {
                    if (choice == 1) updateSelectedCell();
                    else if (choice == 2) restoreBankEdit(false);
                    else if (choice == 3) restoreBankEdit(true);
                });
        };
        bankSelect.onChange = [this] {
            const int choice = bankSelect.getSelectedId();
            if (choice < 1000) { loadSelectedCell(); return; }
            const int selected = bankSelectionBeforeMenu;
            if (choice == 1000) {
                bankSearchDialog = std::make_unique<juce::AlertWindow>(utf8("Buscar no banco"),
                    utf8("Nome, fórmula, origem ou ID. Vazio mostra todo o banco."), juce::MessageBoxIconType::NoIcon);
                bankSearchDialog->addTextEditor("query", bankQuery, utf8("Busca:"));
                bankSearchDialog->addButton(uiText("Buscar", "Search", "Rechercher", "Buscar"), 1, juce::KeyPress(juce::KeyPress::returnKey));
                bankSearchDialog->addButton(uiText("Cancelar", "Cancel", "Annuler", "Cancelar"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
                juce::Component::SafePointer<MainComponent> safeThis(this);
                bankSearchDialog->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, selected](int result) {
                    if (!safeThis || !safeThis->bankSearchDialog) return;
                    if (result == 1) safeThis->bankQuery = safeThis->bankSearchDialog->getTextEditorContents("query").substring(0, 128);
                    safeThis->bankSearchDialog.reset();
                    safeThis->refreshBank(selected, false);
                }), false);
            } else if (choice == 1001) {
                bankFavoritesOnly = !bankFavoritesOnly;
            } else if (choice == 1002 && selected >= 0 && selected < song.cellCount) {
                auto& cell = song.bank[static_cast<size_t>(selected)];
                pushBankUndo(cell);
                cell.favorite = !cell.favorite;
            } else if (choice == 1003) {
                bankQuery.clear(); bankFavoritesOnly = false;
            } else if (choice == 1005) {
                chooseLibraryProject();
            } else if (choice == 1006) {
                openLibrarySearch();
            } else if (choice == 1007) {
                previewLibraryCandidate();
            } else if (choice == 1008) {
                importLibraryCandidate();
            } else if (choice == 1009) {
                libraryIndex.clear(); libraryCandidateSong.reset(); libraryCandidateCell = -1;
                libraryCandidateDescription.clear();
                status.setText(utf8("Índice externo temporário limpo; nenhum arquivo foi alterado."),juce::dontSendNotification);
            } else if (choice == 1010) {
                reindexSavedLibrary();
            } else if (choice == 1011) {
                clearSavedLibrary();
            } else if (choice == 1012) {
                openLibraryGroupSearch();
            } else if (choice == 1013) {
                createLibraryGroup();
            } else if (choice == 1004 && selected >= 0 && selected < song.cellCount) {
                const auto& cell = song.bank[static_cast<size_t>(selected)];
                const auto& origin = cell.formulaOrigin;
                const auto details = "ID #" + juce::String(cell.id) + utf8(" · revisão ") + juce::String(cell.revision) +
                    utf8("\nAutor: ") + juce::String::fromUTF8(origin.author.c_str()) +
                    utf8("\nOrigem: ") + juce::String::fromUTF8(origin.source.c_str()) +
                    utf8("\nID externo: ") + juce::String::fromUTF8(origin.sourceId.c_str()) +
                    utf8("\nLicença registrada: ") + juce::String::fromUTF8(origin.license.c_str()) +
                    utf8("\nCélulas de origem: #") + juce::String(origin.parentA) + " / #" + juce::String(origin.parentB) +
                    utf8("\nSeed: ") + juce::String(cell.phraseSeed) + utf8(" · variação ") + juce::String(cell.phraseVariation) +
                    utf8("\nImportação: ") + juce::String::fromUTF8(cell.importProvenance.c_str()) +
                    utf8("\nCampos vazios indicam informação não registrada.");
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, utf8("Proveniência da célula"), details);
            }
            refreshBank(selected, false);
        };
        placeCellButton.onClick = [this] { placeSelectedCell(); };
        previewCellButton.onClick = [this] { previewSelectedCell(); };
        playSongButton.onClick = [this] { playArrangement(); };
        restartSongButton.onClick = [this] {
            if (directEnabled.load(std::memory_order_acquire)) {
                resetDirect.store(true, std::memory_order_release);
                return;
            }
            if (active.load(std::memory_order_acquire) == nullptr &&
                activeSong.load(std::memory_order_acquire) == nullptr) {
                playArrangement();
                return;
            }
            gridScrollBeat = loopRangeEnabled.load(std::memory_order_acquire)
                ? loopRangeStartBeat : 0.0;
            restart.store(true, std::memory_order_release);
            playing.store(true, std::memory_order_release);
            status.setText(loopRangeEnabled.load(std::memory_order_acquire)
                ? utf8("Reiniciado no começo do trecho em loop.")
                : utf8("Reiniciado do começo da reprodução."), juce::dontSendNotification);
            repaint();
        };
        stopButton.onClick = [this] {
            playing.store(false, std::memory_order_release);
            previewingCell = false;
            directEnabled.store(false, std::memory_order_release);
            directMode.setToggleState(false, juce::dontSendNotification);
            autoMode.setToggleState(false, juce::dontSendNotification);
            autoPhraseButton.setToggleState(false, juce::dontSendNotification);
            loopEnabled.store(false, std::memory_order_release);
            loopMode.setToggleState(false, juce::dontSendNotification);
            active.store(nullptr, std::memory_order_release);
            activeSong.store(nullptr, std::memory_order_release);
            liveInputPlayback.store(nullptr, std::memory_order_release);
            status.setText(utf8("Reprodução parada."), juce::dontSendNotification);
        };
        saveButton.onClick = [this] {
            juce::PopupMenu menu;
            menu.addItem(1, utf8("Salvar projeto editável (.risco)"));
            menu.addSeparator();
            menu.addItem(70, activeCompositionVersion >= 0
                ? utf8("Ramificar versão ativa · guardar snapshot filho")
                : utf8("Guardar versão raiz da composição/mix"), compositionVersions.size() < 16);
            juce::PopupMenu versions;
            versions.addSectionHeader(utf8("Árvore de versões · use Retomar e depois guardar para criar um ramo"));
            for (int i = 0; i < int(compositionVersions.size()); ++i) {
                juce::PopupMenu actions;
                actions.addItem(1000 + i,utf8("Retomar versão"),true,i == activeCompositionVersion);
                actions.addItem(1100 + i,utf8("Remover da lista · só confirma ao salvar o projeto"));
                int depth = 0;
                for (int parent = compositionVersions[static_cast<size_t>(i)].parentIndex;
                     parent >= 0 && parent < i; parent = compositionVersions[static_cast<size_t>(parent)].parentIndex)
                    ++depth;
                juce::String treeLabel;
                for (int level = 0; level < depth; ++level) treeLabel += "  ";
                if (depth > 0) treeLabel += utf8("↳ ");
                treeLabel += compositionVersions[static_cast<size_t>(i)].name;
                versions.addSubMenu(treeLabel,actions);
            }
            menu.addSubMenu(utf8("Retomar versão") + (activeCompositionVersion >= 0 ?
                utf8(" · ") + compositionVersions[static_cast<size_t>(activeCompositionVersion)].name : juce::String()), versions,
                !compositionVersions.empty());
            menu.addItem(71, utf8("Comparar com a versão ativa"), activeCompositionVersion >= 0);
            menu.addItem(72, utf8("Gerar variação por seed…"), compositionVersions.size() < 16);
            menu.addItem(73, utf8("Gerar faixa completa por seed…"));
            risco::SeedTrackOptions savedTrackOptions;
            const bool editableSavedTrack = risco::seedTrackOptionsFromSong(song, savedTrackOptions);
            menu.addItem(76, utf8("Reeditar partes da faixa salva…"), editableSavedTrack);
            const bool albumBusy = (albumJob && albumJob->isThreadRunning()) ||
                                   (albumRenderJob && albumRenderJob->isThreadRunning());
            menu.addItem(74, utf8("Gerar projetos de álbum…"), !albumBusy);
            menu.addItem(75, utf8("Renderizar WAV do álbum…"), !albumBusy);
            juce::PopupMenu formats;
            formats.addItem(16, "PCM 16 bits / 48 kHz", !exportPreMaster, exportBitDepth == 16);
            formats.addItem(24, "PCM 24 bits / 48 kHz", !exportPreMaster, exportBitDepth == 24);
            formats.addItem(32, "Float 32 bits / 48 kHz", true, exportBitDepth == 32);
            menu.addSubMenu(utf8("Formato WAV: ") +
                (exportBitDepth == 32 ? juce::String("Float 32") : "PCM " + juce::String(exportBitDepth)), formats);
            juce::PopupMenu processing;
            processing.addItem(40, utf8("Com MASTER e proteção"), true, !exportPreMaster);
            processing.addItem(41, utf8("Antes do MASTER · Float32"), true, exportPreMaster);
            menu.addSubMenu(utf8("Saída WAV: ") + (exportPreMaster ? utf8("antes do MASTER") :
                utf8("com MASTER")), processing);
            menu.addItem(2, utf8("Exportar áudio estéreo (.wav)"), !exportJob || !exportJob->isThreadRunning());
            menu.addItem(6, utf8("Exportar os cinco stems (.wav)"), !exportJob || !exportJob->isThreadRunning());
            menu.addItem(20, utf8("Reiniciar medidores/hold de saída · sem alterar o áudio"));
            for (int stem = 0; stem < risco::Song::stemCount; ++stem)
                menu.addItem(3 + stem, utf8("Exportar Stem ") + juce::String(stem + 1) + " (.wav)",
                             !exportJob || !exportJob->isThreadRunning());
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&saveButton),
                [this](int choice) {
                    if (choice == 40 || choice == 41) {
                        exportPreMaster = choice == 41;
                        if (exportPreMaster) exportBitDepth = 32;
                        status.setText(exportPreMaster
                            ? utf8("WAV antes do MASTER: Float32 · volume/pan dos stems, sem waveshaper, MASTER e limitador.")
                            : utf8("WAV com MASTER e proteção de saída."), juce::dontSendNotification);
                    }
                    else if (choice == 16 || choice == 24 || choice == 32) {
                        exportBitDepth = choice;
                        status.setText(utf8("Exportação WAV: ") + (choice == 32 ? "Float " : "PCM ") + juce::String(choice) +
                            utf8(" bits / 48 kHz · independente da resolução da síntese."), juce::dontSendNotification);
                    }
                    else if (choice == 1) chooseSongFile(true);
                    else if (choice == 70) saveCompositionVersion();
                    else if (choice == 71) compareCompositionVersion();
                    else if (choice == 72) openCompositionSeedDialog();
                    else if (choice == 73) openSeedTrackDialog();
                    else if (choice == 76) {
                        risco::SeedTrackOptions options;
                        if (risco::seedTrackOptionsFromSong(song, options)) {
                            SeedTrackProposal saved{std::make_shared<risco::Song>(song), options};
                            openSeedTrackPartEditor(utf8("Faixa salva · revisar edição de partes"),
                                                    std::move(saved), {}, 1);
                        }
                    }
                    else if (choice == 74) openAlbumDialog();
                    else if (choice == 75) openAlbumFolderChooser();
                    else if (choice >= 1000 && choice < 1016) restoreCompositionVersion(choice - 1000);
                    else if (choice >= 1100 && choice < 1116) deleteCompositionVersion(choice - 1100);
                    else if (choice == 20) {
                        resetOutputLoudness.store(true, std::memory_order_release);
                        status.setText(utf8("Medidores de saída reiniciados · LUFS, hold de pico e latch de clip; sem alterar o áudio."), juce::dontSendNotification);
                    }
                    else if (choice == 2) chooseAudioExport();
                    else if (choice == 6) chooseAudioExport(-2);
                    else if (choice >= 3 && choice <= 5) chooseAudioExport(choice - 3);
                });
        };
        openButton.setTooltip(utf8("Abrir um arquivo ou retomar um dos oito projetos recentes. A música reabre com o transporte parado."));
        openButton.onClick = [this] {
            const auto recent = readRecentSongs();
            juce::PopupMenu menu;
            menu.addItem(1, utf8("Escolher arquivo (.risco)…"));
            const bool idleRecovery = recoveryPool.getNumJobs() == 0;
            menu.addItem(3, utf8("Criar cópia de recuperação agora"), idleRecovery && !recoveryPending && hasRecoveryData());
            menu.addItem(4, utf8("Recuperar cópia automática (transporte parado)"), idleRecovery && recoveryFile().existsAsFile());
            menu.addItem(5, utf8("Descartar cópia automática"), idleRecovery && recoveryFile().existsAsFile());
            menu.addSeparator();
            menu.addSectionHeader(utf8("Projetos recentes · abrir com transporte parado"));
            for (int i = 0; i < recent.size(); ++i) {
                const juce::File file(recent[i]);
                menu.addItem(10 + i, file.getFileName() + utf8(" — ") + file.getParentDirectory().getFullPathName(),
                             file.existsAsFile());
            }
            menu.addItem(2, utf8("Limpar lista de recentes"), !recent.isEmpty());
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&openButton),
                [this, recent](int choice) {
                    if (choice == 1) chooseSongFile(false);
                    else if (choice == 3) queueRecovery();
                    else if (choice == 4 && recoveryPool.getNumJobs() == 0) loadFromFile(recoveryFile(), true);
                    else if (choice == 5 && recoveryPool.getNumJobs() == 0) {
                        if (recoveryFile().deleteFile()) {
                            recoveryPending = false;
                            recoveryState = std::make_shared<RecoveryState>();
                            status.setText(utf8("Cópia automática descartada; projetos preservados."), juce::dontSendNotification);
                        }
                    }
                    else if (choice == 2) {
                        if (recentSongsFile().replaceWithText("[]"))
                            status.setText(utf8("Lista de recentes limpa; arquivos preservados."), juce::dontSendNotification);
                    }
                    else if (choice >= 10 && choice < 10 + recent.size())
                        loadFromFile(juce::File(recent[choice - 10]));
                });
        };
        removeLastButton.onClick = [this] {
            removeSelectedPlacement();
        };
        cross.onClick = [this] {
            if (inscription.count > 0) {
                pushDrawingUndo();
                auto& stroke = inscription.strokes[static_cast<size_t>(inscription.count - 1)];
                stroke.crossed = !stroke.crossed;
                updateLiveDrawing();
                repaint();
            }
        };
        undoDrawingButton.setTooltip(utf8("Desfaz a última edição do desenho; o botão ao lado refaz."));
        undoDrawingButton.setEnabled(false);
        undoDrawingButton.onClick = [this] { undoDrawingEdit(); };
        redoDrawingButton.setTooltip(utf8("Refaz a última edição desfeita do desenho."));
        redoDrawingButton.setEnabled(false);
        redoDrawingButton.onClick = [this] { redoDrawingEdit(); };
        clear.onClick = [this] {
            pushDrawingUndo();
            playing.store(false, std::memory_order_release);
            previewingCell = false;
            directEnabled.store(false, std::memory_order_release);
            directMode.setToggleState(false, juce::dontSendNotification);
            autoMode.setToggleState(false, juce::dontSendNotification);
            active.store(nullptr, std::memory_order_release);
            activeSong.store(nullptr, std::memory_order_release);
            liveInputPlayback.store(nullptr, std::memory_order_release);
            inscription = {};
            ascii.clear();
            marqueeScrollStartMillis = juce::Time::getMillisecondCounter();
            repaint();
        };
        loadLocalFormulaLibrary();
        refreshRepository();
        updateInterfaceLanguage();
        refreshFormulaGridStatus();
        setSize(1280, 900);
        // The packaged GUI can also be inspected in a container with no
        // audio device; CI uses this to validate window startup independently
        // from ALSA hardware discovery. Normal launches keep audio enabled.
        if (std::getenv("RISCO_TEST_NO_AUDIO") == nullptr)
            setAudioChannels(0, 2);
        if (recoveryPending)
            status.setText(utf8("Há uma cópia automática anterior. Abra Abrir para recuperar ou descartar; ela será preservada até sua escolha."),
                           juce::dontSendNotification);
        startTimerHz(30);
    }

    ~MainComponent() override {
        libraryPool.removeAllJobs(true, 10000);
        recoveryPool.removeAllJobs(true, 10000);
        if (exportJob) { exportJob->completed = {}; exportJob.reset(); }
        if (albumJob) { albumJob->completed = {}; albumJob.reset(); }
        if (albumRenderJob) { albumRenderJob->completed = {}; albumRenderJob.reset(); }
        stopTimer(); shutdownAudio(); setLookAndFeel(nullptr);
    }

    void prepareToPlay(int, double rate) override {
        sampleRate = rate > 0.0 ? rate : 48000.0;
        historicalPreparedTimbre = 0;
        outputStage.prepare(sampleRate);
        for (auto& effect : stemEffects) effect.prepare(sampleRate);
        for (auto& automation : stemAutoProcessors) automation.prepare(sampleRate);
        for (auto& automation : stemPanAutoProcessors) automation.prepare(sampleRate);
        outputTruePeak.store(0.0f, std::memory_order_relaxed);
        outputTruePeakHold.store(0.0f, std::memory_order_relaxed);
        outputReductionDb.store(0.0f, std::memory_order_relaxed);
        outputRms.store(0.0f, std::memory_order_relaxed);
        outputCorrelation.store(0.0f, std::memory_order_relaxed);
        outputMonoRms.store(0.0f, std::memory_order_relaxed);
        outputClipLatched.store(false, std::memory_order_relaxed);
    }
    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        AudioReadGuard audioRead(audioReaders);
        juce::ScopedNoDenormals noDenormals;
        if (resetOutputLoudness.exchange(false, std::memory_order_acq_rel)) outputStage.resetReadouts();
        info.clearActiveBufferRegion();
        const auto writeSample = [this, &info](int i, risco::OutputStage::Stereo sample) {
            const float mono = (sample.left + sample.right) * 0.5f;
            scope[static_cast<size_t>(scopeWriteIndex++ % scope.size())]
                .store(mono, std::memory_order_relaxed);
            for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
                info.buffer->setSample(channel, info.startSample + i,
                                       channel == 0 ? sample.left : channel == 1 ? sample.right : mono);
        };
        const auto writeOutput = [this, &writeSample](int i, float left, float right, float master) {
            writeSample(i, outputStage.process(left, right, master));
        };
        const auto publishOutputMeter = [this](const risco::OutputStage::Meter& meter) {
            outputTruePeak.store(meter.truePeak, std::memory_order_release);
            outputTruePeakHold.store(meter.truePeakHold, std::memory_order_release);
            outputReductionDb.store(meter.reductionDb, std::memory_order_release);
            outputRms.store(meter.rms, std::memory_order_release);
            outputCorrelation.store(meter.correlation, std::memory_order_release);
            outputMonoRms.store(meter.monoRms, std::memory_order_release);
            outputClipLatched.store(meter.clipLatched, std::memory_order_release);
            outputLufsM.store(meter.loudness.momentaryLufs, std::memory_order_release);
            outputLufsS.store(meter.loudness.shortTermLufs, std::memory_order_release);
            outputLufsI.store(meter.loudness.integratedLufs, std::memory_order_release);
            outputLufsMReady.store(meter.loudness.momentaryReady, std::memory_order_release);
            outputLufsSReady.store(meter.loudness.shortTermReady, std::memory_order_release);
            outputLufsIReady.store(meter.loudness.integratedBlocks > 0, std::memory_order_release);
        };
        if (auto* preview = seedTrackPreview.get()) {
            if (preview->rateHz != sampleRate) preview->frame = preview->endFrame;
            const bool hadAudio = preview->frame < preview->endFrame;
            for (int i = 0; i < info.numSamples; ++i) {
                const auto sample = preview->frame < preview->endFrame
                    ? preview->renderer.frame(preview->frame++) : risco::OutputStage::Stereo{};
                writeSample(i, sample);
            }
            seedTrackPreviewPlaying.store(preview->frame < preview->endFrame, std::memory_order_relaxed);
            publishedScopeWriteIndex.store(scopeWriteIndex, std::memory_order_release);
            publishOutputMeter(hadAudio ? preview->renderer.takeMeterWindow() : risco::OutputStage::Meter{});
            return;
        }
        const auto* sequence = active.load(std::memory_order_acquire);
        const auto* arrangement = activeSong.load(std::memory_order_acquire);
        const auto* liveArrangement = liveInputPlayback.load(std::memory_order_acquire);
        const auto* formula = activeFormula.load(std::memory_order_acquire);
        const bool direct = directEnabled.load(std::memory_order_acquire);
        if (!playing.load(std::memory_order_acquire) ||
            (!direct && sequence == nullptr && arrangement == nullptr && liveArrangement == nullptr)) {
            for (int i = 0; i < info.numSamples; ++i) writeOutput(i, 0.0f, 0.0f, 0.0f);
            publishedScopeWriteIndex.store(scopeWriteIndex, std::memory_order_release);
            publishOutputMeter(outputStage.takeMeterWindow());
            return;
        }
        if (direct) {
            if (resetDirect.exchange(false, std::memory_order_acq_rel)) {
                directSampleIndex = 0;
                directHasValue = false;
            }
            for (int i = 0; i < info.numSamples; ++i) {
                const auto t = risco::directBytebeatTime(directSampleIndex++, sampleRate);
                if (!directHasValue || t != directLastTime) {
                    directLastValue = risco::directBytebeatSample(formula, t);
                    directLastTime = t;
                    directHasValue = true;
                }
                const float fade = std::min(1.0f, float(directSampleIndex) / float(sampleRate * 0.01));
                const float shaped = risco::shapeSample(directLastValue, renderingShape.load(), renderingDrive.load());
                writeOutput(i, shaped * 0.20f * fade, shaped * 0.20f * fade,
                            masterGain.load(std::memory_order_relaxed));
            }
            displayedDirectTime.store(risco::directBytebeatTime(directSampleIndex, sampleRate),
                                      std::memory_order_relaxed);
            publishedScopeWriteIndex.store(scopeWriteIndex, std::memory_order_release);
            publishOutputMeter(outputStage.takeMeterWindow());
            return;
        }
        const bool useFormula = formulaEnabled.load(std::memory_order_relaxed);
        if (restart.exchange(false, std::memory_order_acq_rel)) {
            sampleIndex = 0;
            for (auto& effect : stemEffects) effect.reset();
            for (auto& automation : stemAutoProcessors) automation.reset();
            for (auto& automation : stemPanAutoProcessors) automation.reset();
            for (auto& engine : historicalStemEngines) engine.reset();
            for (auto& engine : liveHistoricalStemEngines) engine.reset();
            historicalPreviewEngine.reset();
        }
        double displayedTime = playheadSeconds.load(std::memory_order_relaxed);
        std::array<risco::GainAutomation, risco::Song::stemCount> autoCurves;
        std::array<risco::PanAutomation, risco::Song::stemCount> panCurves;
        std::array<risco::FxAutomation, risco::Song::stemCount> fxCurves;
        std::array<risco::ParameterAutomation, risco::Song::stemCount> filterCurves;
        std::array<risco::ParameterAutomation, risco::Song::stemCount> delayMixCurves, delayFeedbackCurves, reverbMixCurves, reverbDecayCurves;
        std::array<risco::ParameterAutomation, risco::Song::stemCount> bitcrushCurves;
        const bool applyAutomation = arrangement != nullptr ? arrangement->applyStemAutomation
            : liveArrangement != nullptr && liveArrangement->applyStemAutomation;
        for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
            const auto index = static_cast<size_t>(stem);
            autoCurves[index].enabled = applyAutomation && stemAutoState[index].enabled.load(std::memory_order_acquire);
            for (size_t p = 0; p < 3; ++p) autoCurves[index].levels[p] = stemAutoState[index].levels[p].load(std::memory_order_relaxed);
            panCurves[index].enabled = applyAutomation && stemPanAutoState[index].enabled.load(std::memory_order_acquire);
            fxCurves[index].enabled = applyAutomation && stemFxAutoState[index].enabled.load(std::memory_order_acquire);
            filterCurves[index].enabled = applyAutomation && stemFilterAutoState[index].enabled.load(std::memory_order_acquire);
            delayMixCurves[index].enabled = applyAutomation && stemDelayAutoState[index].mixEnabled.load(std::memory_order_acquire);
            delayFeedbackCurves[index].enabled = applyAutomation && stemDelayAutoState[index].feedbackEnabled.load(std::memory_order_acquire);
            reverbMixCurves[index].enabled = applyAutomation && stemReverbAutoState[index].enabled.load(std::memory_order_acquire);
            reverbDecayCurves[index].enabled = applyAutomation && stemReverbAutoState[index].decayEnabled.load(std::memory_order_acquire);
            bitcrushCurves[index].enabled = applyAutomation && stemBitcrushAutoState[index].enabled.load(std::memory_order_acquire);
            for (size_t p = 0; p < 3; ++p) {
                panCurves[index].levels[p] = stemPanAutoState[index].levels[p].load(std::memory_order_relaxed);
                fxCurves[index].active[p] = stemFxAutoState[index].active[p].load(std::memory_order_relaxed);
                filterCurves[index].values[p] = stemFilterAutoState[index].values[p].load(std::memory_order_relaxed);
                delayMixCurves[index].values[p] = stemDelayAutoState[index].mix[p].load(std::memory_order_relaxed);
                delayFeedbackCurves[index].values[p] = stemDelayAutoState[index].feedback[p].load(std::memory_order_relaxed);
                reverbMixCurves[index].values[p] = stemReverbAutoState[index].mix[p].load(std::memory_order_relaxed);
                reverbDecayCurves[index].values[p] = stemReverbAutoState[index].decay[p].load(std::memory_order_relaxed);
                bitcrushCurves[index].values[p] = stemBitcrushAutoState[index].bits[p].load(std::memory_order_relaxed);
            }
            stemEffects[static_cast<size_t>(stem)].setTempo(renderingBpm.load(std::memory_order_relaxed));
            stemEffects[static_cast<size_t>(stem)].setReverbMix(stemReverbMixValue[static_cast<size_t>(stem)].load(std::memory_order_relaxed));
            stemEffects[static_cast<size_t>(stem)].setReverbDecay(stemReverbDecayValue[static_cast<size_t>(stem)].load(std::memory_order_relaxed));
        }
        for (int i = 0; i < info.numSamples; ++i) {
            const double absoluteTime = double(sampleIndex++) / sampleRate;
            const double duration = arrangement != nullptr ? arrangement->end
                : liveArrangement != nullptr ? liveArrangement->end : sequence->end;
            const double playbackDuration = arrangement == nullptr && liveArrangement != nullptr && sequence != nullptr
                ? std::max(duration, sequence->end) : duration;
            const bool loopRange = arrangement != nullptr &&
                loopRangeEnabled.load(std::memory_order_acquire);
            const double rangeStart = loopRangeStartSeconds.load(std::memory_order_relaxed);
            const double rangeEnd = loopRangeEndSeconds.load(std::memory_order_relaxed);
            const double rangeLength = rangeEnd - rangeStart;
            const bool validLoopRange = loopRange && rangeStart >= 0.0 && rangeLength > 0.0 &&
                                        rangeStart < playbackDuration;
            const bool loop = validLoopRange || loopEnabled.load(std::memory_order_relaxed);
            const double t = validLoopRange
                ? rangeStart + std::fmod(absoluteTime, rangeLength)
                : loop && playbackDuration > 0.0 ? std::fmod(absoluteTime, playbackDuration) : absoluteTime;
            displayedTime = t;
            if (!loop && t >= playbackDuration) {
                playing.store(false, std::memory_order_release);
                for (; i < info.numSamples; ++i) writeOutput(i, 0.0f, 0.0f, 0.0f);
                break;
            }
            const int timbre = renderingTimbre.load(std::memory_order_relaxed);
            const bool historical = risco::isHistoricalAyTimbre(timbre);
            if (historical && historicalPreparedTimbre != timbre) {
                const auto model = risco::historicalAyModel(timbre);
                for (auto& engine : historicalStemEngines) engine.prepare(model, sampleRate);
                for (auto& engine : liveHistoricalStemEngines) engine.prepare(model, sampleRate);
                historicalPreviewEngine.prepare(model, sampleRate);
                historicalPreparedTimbre = timbre;
            } else if (!historical) historicalPreparedTimbre = 0;
            const risco::HistoricalAySettings historicalSettings{
                renderingHistoricalAyTexture.load(std::memory_order_relaxed),
                renderingHistoricalAyNoisePeriod.load(std::memory_order_relaxed),
                renderingHistoricalAyEnvelope.load(std::memory_order_relaxed),
                renderingHistoricalAyEnvelopeRamp.load(std::memory_order_relaxed)};
            const auto mode = timbre > 0 ? risco::noteWaveMode(timbre) : useFormula ? risco::WaveMode::phaseFunction : risco::WaveMode::quantizedSine;
            if (arrangement != nullptr || liveArrangement != nullptr) {
                float left = 0.0f;
                float right = 0.0f;
                for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
                    const auto index = static_cast<size_t>(stem);
                    bool anySolo = false;
                    for (const auto& solo : stemSoloed) anySolo = anySolo || solo.load(std::memory_order_relaxed);
                    const bool muted = stemMuted[static_cast<size_t>(stem)].load(std::memory_order_relaxed);
                    const bool soloExcluded = anySolo && !stemSoloed[static_cast<size_t>(stem)].load(std::memory_order_relaxed);
                    const int selectedEffect = stemEffectValue[index].load(std::memory_order_relaxed);
                    stemEffects[index].setPreset(risco::automationFxActiveAt(fxCurves[index],t,duration) ? selectedEffect : 0);
                    stemEffects[index].setFilterCutoff(risco::automationParameterAt(filterCurves[index], t, duration,
                        stemFilterCutoffValue[index].load(std::memory_order_relaxed), 80.0f, 16000.0f));
                    stemEffects[index].setDelayParameters(
                        risco::automationParameterAt(delayMixCurves[index], t, duration, stemDelayMixValue[index].load(std::memory_order_relaxed), 0.0f, 0.6f),
                        risco::automationParameterAt(delayFeedbackCurves[index], t, duration, stemDelayFeedbackValue[index].load(std::memory_order_relaxed), 0.0f, 0.6f));
                    stemEffects[index].setReverbMix(risco::automationParameterAt(reverbMixCurves[index], t, duration,
                        stemReverbMixValue[index].load(std::memory_order_relaxed), 0.0f, 0.6f));
                    if (reverbDecayCurves[index].enabled)
                        stemEffects[index].setReverbDecay(risco::automationReverbDecayAt(reverbDecayCurves[index], t, duration,
                            stemReverbDecayValue[index].load(std::memory_order_relaxed)));
                    if (bitcrushCurves[index].enabled)
                        stemEffects[index].setBitcrushBits(risco::automationBitcrushBitsAt(bitcrushCurves[index], t, duration,
                            stemBitcrushBitsValue[index].load(std::memory_order_relaxed)));
                    float source = 0.0f;
                    const double liveTime = liveArrangement != nullptr && liveArrangement->end > 0.0
                        ? std::fmod(absoluteTime, liveArrangement->end) : t;
                    if (historical) {
                        if (arrangement != nullptr) {
                            const auto chip = historicalStemEngines[index].processStem(*arrangement, t, stem,
                                historicalSettings);
                            source += (chip.left + chip.right) * 0.5f +
                                risco::songStemDirectFormulaSampleAt(*arrangement, t, stem, absoluteTime);
                        }
                        if (liveArrangement != nullptr) {
                            const auto chip = liveHistoricalStemEngines[index].processStem(*liveArrangement, liveTime,
                                stem, historicalSettings);
                            source += (chip.left + chip.right) * 0.5f +
                                risco::songStemDirectFormulaSampleAt(*liveArrangement, liveTime, stem, absoluteTime);
                        }
                    } else {
                        if (arrangement != nullptr)
                            source += risco::songStemSampleAt(*arrangement, t, stem, mode, formula, absoluteTime,
                                renderingAyNoisePeriod.load(std::memory_order_relaxed),
                                renderingAyEnvelopeMode.load(std::memory_order_relaxed),
                                renderingAyEnvelopeRamp.load(std::memory_order_relaxed));
                        if (liveArrangement != nullptr)
                            source += risco::songStemSampleAt(*liveArrangement, liveTime, stem, mode, formula,
                                absoluteTime, renderingAyNoisePeriod.load(std::memory_order_relaxed),
                                renderingAyEnvelopeMode.load(std::memory_order_relaxed),
                                renderingAyEnvelopeRamp.load(std::memory_order_relaxed));
                    }
                    const float raw = stemEffects[index].process(source *
                        stemAutoProcessors[index].process(autoCurves[index], t, duration));
                    if (muted || soloExcluded) continue;
                    const float level = stemGainValue[index].load(std::memory_order_relaxed);
                    const float pan = stemPanAutoProcessors[index].process(panCurves[index],t,duration,
                        stemPanValue[index].load(std::memory_order_relaxed));
                    const float angle = (pan+1.0f)*0.78539816339f;
                    left += raw*level*std::cos(angle);
                    right += raw*level*std::sin(angle);
                }
                if (sequence != nullptr) {
                    float rawSequence = 0.0f;
                    if (historical) {
                        const auto chip = historicalPreviewEngine.process(*sequence, t, historicalSettings);
                        rawSequence = (chip.left + chip.right) * 0.5f;
                    } else rawSequence = risco::sampleAt(*sequence, t, mode, formula,
                        renderingBits.load(std::memory_order_relaxed),
                        renderingAyNoisePeriod.load(std::memory_order_relaxed),
                        renderingAyEnvelopeMode.load(std::memory_order_relaxed),
                        renderingAyEnvelopeRamp.load(std::memory_order_relaxed));
                    left += rawSequence;
                    right += rawSequence;
                }
                const int shape = renderingShape.load(std::memory_order_relaxed);
                if (shape != 0) {
                    const float drive = renderingDrive.load(std::memory_order_relaxed);
                    left = risco::shapeSample(std::clamp(left, -1.0f, 1.0f), shape, drive);
                    right = risco::shapeSample(std::clamp(right, -1.0f, 1.0f), shape, drive);
                }
                writeOutput(i, left, right, masterGain.load(std::memory_order_relaxed));
                continue;
            }
            if (historical) {
                const auto chip = historicalPreviewEngine.process(*sequence, t, historicalSettings);
                const float left = risco::shapeSample(chip.left, renderingShape.load(std::memory_order_relaxed),
                    renderingDrive.load(std::memory_order_relaxed));
                const float right = risco::shapeSample(chip.right, renderingShape.load(std::memory_order_relaxed),
                    renderingDrive.load(std::memory_order_relaxed));
                writeOutput(i, left, right, masterGain.load(std::memory_order_relaxed));
            } else {
                const float rawSample = risco::sampleAt(*sequence, t, mode, formula,
                    renderingBits.load(std::memory_order_relaxed), renderingAyNoisePeriod.load(std::memory_order_relaxed),
                    renderingAyEnvelopeMode.load(std::memory_order_relaxed), renderingAyEnvelopeRamp.load(std::memory_order_relaxed));
                const float shaped = risco::shapeSample(rawSample, renderingShape.load(std::memory_order_relaxed),
                    renderingDrive.load(std::memory_order_relaxed));
                writeOutput(i, shaped, shaped, masterGain.load(std::memory_order_relaxed));
            }
        }
        playheadSeconds.store(displayedTime, std::memory_order_relaxed);
        publishedScopeWriteIndex.store(scopeWriteIndex, std::memory_order_release);
        publishOutputMeter(outputStage.takeMeterWindow());
    }

    void paint(juce::Graphics& g) override {
        g.addTransform(juce::AffineTransform::scale(scaleX(), scaleY()));
        g.fillAll(juce::Colour(0xff7893a5));
        g.setColour(juce::Colour(0xff101b2d));
        g.fillRect(0, 0, logicalWidth, 48);
        g.setColour(juce::Colour(0xff28516c));
        g.drawHorizontalLine(47, 0.0f, float(logicalWidth));
        // Colored paths group controls by their role in the composition flow.
        const auto flowBand = [&g](ControlBand band, juce::Colour colour) {
            const float x = float(band.x), y = float(band.y);
            const float w = float(band.width), h = float(band.height);
            const auto box = juce::Rectangle<float>(x, y, w, h);
            g.setColour(colour.withAlpha(0.11f));
            g.fillRoundedRectangle(box, 5.0f);
            g.setColour(colour.withAlpha(0.40f));
            g.drawRoundedRectangle(box.reduced(0.5f), 5.0f, 1.0f);
            g.setColour(colour);
            g.fillRoundedRectangle(x, y + 4.0f, 3.0f, h - 8.0f, 1.5f);
        };
        const auto livePath = juce::Colour(0xff65d9e8);
        const auto seedPath = juce::Colour(0xffc4a1ff);
        const auto bankPath = juce::Colour(0xff7899ff);
        const auto gridPath = juce::Colour(0xffffbf55);
        flowBand(seedBand, seedPath);
        flowBand(liveBand, livePath);
        flowBand(bankBand, bankPath);
        flowBand(formulaBand, seedPath);
        g.setColour(juce::Colour(0xff65d9e8).withAlpha(0.84f));
        g.drawRoundedRectangle(float(formulaEditorX - 4), float(formulaFirstRowY - 2),
                               float(formulaEditorWidth + 8), 34.0f, 5.0f, 2.0f);
        flowBand(gridControlsBand, gridPath);
        const float brandHorizontalScale = scaleY() / scaleX();
        constexpr float brandIconSize = 48.0f;
        const float brandIconX = 20.0f - brandIconSize * (4.0f / 64.0f) *
                                         brandHorizontalScale;
        const float brandVisibleX = brandIconX + brandIconSize * (4.0f / 64.0f) *
                                                 brandHorizontalScale;
        constexpr float brandIconY = 7.0f - brandIconSize * (18.0f / 64.0f);
        risco::app::paintRiscoWaveIcon(g, brandIconX, brandIconY, brandIconSize,
                                      brandHorizontalScale);
        constexpr float brandGap = 6.0f;
        const float brandWordX = brandIconX + brandIconSize * (60.0f / 64.0f) *
                                              brandHorizontalScale +
                                 brandGap * brandHorizontalScale;
        risco::app::paintRiscoPixelLogo(g, brandWordX, 7.0f, brandHorizontalScale);
        const std::array<std::pair<juce::String, juce::Colour>, 4> flowLegend{{
            {uiText("AO VIVO", "LIVE", "DIRECT", "EN VIVO"), livePath},
            {uiText("GERAR", "GENERATE", "GÉNÉRER", "GENERAR"), seedPath},
            {uiText("BANCO", "BANK", "BANQUE", "BANCO"), bankPath},
            {uiText("GRADE", "GRID", "GRILLE", "CUADRÍCULA"), gridPath}}};
        g.setFont(canvasFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold))));
        int legendX = 250;
        for (const auto& item : flowLegend) {
            g.setColour(item.second);
            g.fillRect(float(legendX), 35.0f, 6.0f, 6.0f);
            drawCanvasText(g, item.first, legendX + 10, 31, 53, 15, juce::Justification::left);
            legendX += 65;
        }
        g.setColour(juce::Colour(0xff91adbd));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(10.0f))));
        drawCanvasText(g, uiText("INSTRUMENTO GENERATIVO  ·  RASGO", "GENERATIVE INSTRUMENT  ·  RASGO",
            "INSTRUMENT GÉNÉRATIF  ·  RASGO", "INSTRUMENTO GENERATIVO  ·  RASGO"),
                   juce::roundToInt(brandVisibleX), 31, 300, 13,
                   juce::Justification::left);
        const auto marquee = juce::Rectangle<int>(80, 242, logicalWidth - 160, 68);
        g.setColour(juce::Colour(0xff071522));
        g.fillRoundedRectangle(marquee.toFloat(), 7.0f);
        g.setColour(juce::Colour(0xff65d9e8).withAlpha(0.48f));
        g.drawRoundedRectangle(marquee.toFloat().reduced(0.5f), 7.0f, 1.0f);
        g.setColour(juce::Colour(0xff65d9e8).withAlpha(0.24f));
        for (int i = 1; i < 16; ++i) {
            const float x = float(marquee.getX()) + marquee.getWidth() * float(i) / 16.0f;
            g.fillRect(x - 1.0f, float(marquee.getBottom() - 7), 2.0f, 2.0f);
        }
        const auto marqueeText = ascii.getText().replaceCharacters("\r\n\t", "   ");
        const bool drawOnLed = !textEntryMode && (ledShowsDrawing || marqueeText.isEmpty());
        const bool directReadout = directEnabled.load(std::memory_order_relaxed);
        const auto audioIndicator = currentAudioIndicator();
        g.setColour(audioIndicator.second);
        g.fillEllipse(float(marquee.getX() + 10), float(marquee.getY() + 8), 10.0f, 10.0f);
        g.setColour(juce::Colour(0xffe3edf2));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold))));
        drawCanvasText(g, audioIndicator.first,
                   marquee.getX() + 28, marquee.getY() + 3, marquee.getWidth() - 170, 20,
                   juce::Justification::left, true);
        g.setColour(juce::Colour(0xff55d6e8));
        drawCanvasText(g, directReadout ? uiText("LEITOR · BYTEBEAT", "READER · BYTEBEAT", "LECTEUR · BYTEBEAT", "LECTOR · BYTEBEAT") :
                   drawOnLed ? uiText("LEITOR · DESENHO", "READER · DRAWING", "LECTEUR · DESSIN", "LECTOR · DIBUJO") :
                               uiText("LEITOR · TEXTO", "READER · TEXT", "LECTEUR · TEXTE", "LECTOR · TEXTO"),
                   marquee.getRight() - 140, marquee.getY() + 3, 126, 20,
                   juce::Justification::right, true);
        const float panelX = float(marquee.getX() + 16), panelW = float(marquee.getWidth() - 32);
        const float dialY = float(marquee.getY() + 18), dialHeight = 44.0f;
        const auto now = juce::Time::getMillisecondCounter();
        const auto elapsedMillis = static_cast<std::uint32_t>(now - marqueeScrollStartMillis);
        float dialX = float(marquee.getCentreX());
        if (!directEnabled.load(std::memory_order_relaxed) && !drawOnLed && marqueeText.isNotEmpty()) {
            const auto marqueeFont = canvasFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
            const float textWidth = std::max(1.0f,
                juce::TextLayout::getStringWidth(
                    marqueeFont.withHeight(marqueeFont.getHeight() * scaleY()), marqueeText) / scaleX());
            float textX = panelX + panelW * 0.5f;
            if (textWidth <= panelW - 48.0f) {
                textX = panelX + (panelW - textWidth) * 0.5f;
                const float sweep = textWidth + 24.0f;
                const float moveMillis = sweep / 56.0f * 1000.0f;
                const float phase = std::fmod(float(elapsedMillis), moveMillis * 2.0f + 900.0f);
                if (phase < 450.0f) dialX = textX - 12.0f;
                else if (phase < 450.0f + moveMillis) dialX = textX - 12.0f + (phase - 450.0f) * 0.056f;
                else if (phase < 900.0f + moveMillis) dialX = textX + textWidth + 12.0f;
                else dialX = textX + textWidth + 12.0f - (phase - 900.0f - moveMillis) * 0.056f;
            } else {
                constexpr float readSpeed = 28.0f;
                const float moveMillis = (textWidth + panelW * 0.5f) / readSpeed * 1000.0f;
                const float phase = std::fmod(float(elapsedMillis), moveMillis + 900.0f);
                textX = phase < moveMillis
                    ? panelX + panelW * 0.5f - phase * (readSpeed / 1000.0f)
                    : panelX - textWidth;
            }
            g.saveState();
            g.reduceClipRegion(marquee.getX() + 8, marquee.getY() + 17, marquee.getWidth() - 16, 44);
            g.setFont(marqueeFont);
            g.setColour(juce::Colour(0xff9cf6c8).withAlpha(0.18f));
            drawCanvasText(g, marqueeText, int(textX) - 1, marquee.getY() + 13,
                       int(std::ceil(textWidth)) + 2, 44, juce::Justification::left);
            g.setColour(juce::Colour(0xff9cf6c8));
            drawCanvasText(g, marqueeText, int(textX), marquee.getY() + 14,
                       int(std::ceil(textWidth)) + 2, 42, juce::Justification::left);
            g.restoreState();
        } else if (!directEnabled.load(std::memory_order_relaxed) && drawOnLed && inscription.count > 0) {
            int total = 0;
            for (int s = 0; s < inscription.count; ++s)
                total += inscription.strokes[static_cast<size_t>(s)].count;
            const float pathWidth = float(std::max(1, total * 8));
            const float moveMillis = (pathWidth + panelW * 0.5f) / 28.0f * 1000.0f;
            const float phase = std::fmod(float(elapsedMillis), moveMillis + 900.0f);
            const float scrollX = phase < moveMillis
                ? panelX + panelW * 0.5f - phase * 0.028f
                : panelX - pathWidth;
            g.saveState();
            g.reduceClipRegion(marquee.getX() + 8, marquee.getY() + 17, marquee.getWidth() - 16, 44);
            int offset = 0;
            for (int s = 0; s < inscription.count; ++s) {
                const auto& stroke = inscription.strokes[static_cast<size_t>(s)];
                if (stroke.count < 2) { offset += stroke.count; continue; }
                juce::Path path;
                for (int i = 0; i < stroke.count; ++i) {
                    const auto& p = stroke.points[static_cast<size_t>(i)];
                    const float x = scrollX + float((offset + i) * 8);
                    const float y = float(marquee.getY() + 18) + p.y * 36.0f;
                    if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
                }
                g.setColour(juce::Colour(stroke.crossed ? crossedGraffitiColour :
                    graffitiColours[static_cast<std::size_t>(s) % graffitiColours.size()]));
                g.strokePath(path, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                offset += stroke.count;
            }
            g.restoreState();
        }
        if (!directReadout) {
            g.setColour(juce::Colour(0xffffc65c).withAlpha(0.12f));
            g.fillRect(dialX - 9.0f, dialY, 18.0f, dialHeight);
            g.setColour(juce::Colour(0xffffc65c));
            g.fillRect(dialX - 1.5f, dialY, 3.0f, dialHeight);
            g.fillEllipse(dialX - 4.0f, dialY - 2.0f, 8.0f, 5.0f);
            g.fillEllipse(dialX - 4.0f, dialY + dialHeight - 3.0f, 8.0f, 5.0f);
        }
        paintDrawing(g);
        paintGrid(g);
        paintScope(g);
        g.setColour(juce::Colour(0xff283e50));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(11.0f))));
        drawCanvasText(g, utf8("© Lúcio de Araújo · Risco 2026 · RASGO · GNU AGPL-3.0-or-later"),
                   20, logicalHeight - 22, logicalWidth - 40, 16, juce::Justification::centredLeft);
    }

    void resized() override {
        constexpr int seedY = centredControlY(seedBand);
        constexpr int liveY = centredControlY(liveBand);
        constexpr int bankY = centredControlY(bankBand);
        constexpr int gridY = centredControlY(gridControlsBand);
        const bool compact = getWidth() < 1200;
        place(ascii, drawingArea().getX(), drawingArea().getY(),
              drawingArea().getWidth(), drawingArea().getHeight());
        if (compact) {
            place(seedSource, 20, seedY, 140, controlHeight);
            place(seedInput, 164, seedY, 92, controlHeight);
            place(generateFormulaButton, 260, seedY, 120, controlHeight);
            place(functionMode, 384, seedY, 150, controlHeight);
            place(loopMode, 538, seedY, 140, controlHeight);
            place(autoMode, 682, seedY, 150, controlHeight);
            place(formulaTonicSelect, 836, seedY, 180, controlHeight);
            place(responseModeSelect, 1020, seedY, 236, controlHeight);
            place(masterLabel, 250, 5, 70, 24);
            place(masterSlider, 324, 3, 188, 26);
        } else {
            place(seedSource, 20, seedY, 140, controlHeight);
            place(seedInput, 168, seedY, 92, controlHeight);
            place(generateFormulaButton, 268, seedY, 120, controlHeight);
            place(functionMode, 396, seedY, 150, controlHeight);
            place(loopMode, 554, seedY, 140, controlHeight);
            place(autoMode, 702, seedY, 150, controlHeight);
            place(formulaTonicSelect, 870, seedY, 170, controlHeight);
            place(responseModeSelect, 1050, seedY, 205, controlHeight);
            place(masterLabel, 250, 5, 60, 24);
            place(masterSlider, 312, 3, 200, 26);
        }
        place(ledSourceButton, 20, liveY, 145, controlHeight);
        place(play, 175, liveY, 160, controlHeight);
        place(cross, 345, liveY, 98, controlHeight);
        place(clear, 450, liveY, 65, controlHeight);
        place(undoDrawingButton, 520, liveY, 60, controlHeight);
        place(redoDrawingButton, 585, liveY, 60, controlHeight);
        place(glyph, 650, liveY, 135, controlHeight);
        if (compact) {
            place(cellSpan, 20, bankY, 140, controlHeight);
            place(repetitions, 164, bankY, 120, controlHeight);
            place(cellName, 288, bankY, 150, controlHeight);
            place(storeCellButton, 442, bankY, 125, controlHeight);
            place(bankSelect, 571, bankY, 150, controlHeight);
            place(stemSelect, 725, bankY, 106, controlHeight);
            place(autoRouteButton, 835, bankY, 58, controlHeight);
            place(updateCellButton, 897, bankY, 78, controlHeight);
            place(roleSelect, 979, bankY, 112, controlHeight);
            place(timbreSelect, 1095, bankY, 165, controlHeight);
        } else {
            place(cellSpan, 20, bankY, 150, controlHeight);
            place(repetitions, 185, bankY, 130, controlHeight);
            place(cellName, 330, bankY, 150, controlHeight);
            place(storeCellButton, 490, bankY, 130, controlHeight);
            place(bankSelect, 630, bankY, 145, controlHeight);
            place(stemSelect, 785, bankY, 90, controlHeight);
            place(autoRouteButton, 879, bankY, 66, controlHeight);
            place(updateCellButton, 949, bankY, 78, controlHeight);
            place(roleSelect, 1031, bankY, 108, controlHeight);
            place(timbreSelect, 1143, bankY, 117, controlHeight);
        }
        place(formulaInput, formulaEditorX, formulaFirstRowY, formulaEditorWidth, controlHeight);
        place(applyFormula, 690, formulaFirstRowY, 140, controlHeight);
        place(directMode, 840, formulaFirstRowY, 160, controlHeight);
        place(resetDirectButton, 1010, formulaFirstRowY, 110, controlHeight);
        place(bitDepthSelect, 1130, formulaFirstRowY, 130, controlHeight);
        place(shapeSelect, 805, liveY, 190, controlHeight);
        place(shapeDriveLabel, 1005, centredControlY(liveBand, 24), 48, 24);
        place(shapeDriveSlider, 1055, liveY, 205, controlHeight);
        if (compact) {
            place(fontSelect, 20, formulaSecondRowY, 170, controlHeight);
            place(previewCellButton, 193, formulaSecondRowY, 145, controlHeight);
            place(formulaToNotesButton, 341, formulaSecondRowY, 125, controlHeight);
            place(formulaScaleSelect, 469, formulaSecondRowY, 160, controlHeight);
            place(formulaMotionSelect, 632, formulaSecondRowY, 180, controlHeight);
            place(formulaGroupingSelect, 815, formulaSecondRowY, 135, controlHeight);
            place(saveFormulaNotesButton, 953, formulaSecondRowY, 150, controlHeight);
            place(formulaGridStatus, 1106, formulaSecondRowY, 154, controlHeight);
        } else {
            place(fontSelect, 20, formulaSecondRowY, 180, controlHeight);
            place(previewCellButton, 210, formulaSecondRowY, 145, controlHeight);
            place(formulaToNotesButton, 365, formulaSecondRowY, 125, controlHeight);
            place(formulaScaleSelect, 500, formulaSecondRowY, 150, controlHeight);
            place(formulaMotionSelect, 660, formulaSecondRowY, 170, controlHeight);
            place(formulaGroupingSelect, 840, formulaSecondRowY, 125, controlHeight);
            place(saveFormulaNotesButton, 975, formulaSecondRowY, 135, controlHeight);
            place(formulaGridStatus, 1120, formulaSecondRowY, 140, controlHeight);
        }
        place(startBeat, 20, gridY, 100, controlHeight);
        place(placeCellButton, 130, gridY, 145, controlHeight);
        place(saveButton, 285, gridY, 105, controlHeight);
        place(openButton, 400, gridY, 105, controlHeight);
        place(removeLastButton, 515, gridY, 105, controlHeight);
        place(varyPhraseButton, 630, gridY, 94, controlHeight);
        place(autoPhraseButton, 730, gridY, 90, controlHeight);
        place(bpmLabel, 835, centredControlY(gridControlsBand, 24), 45, 24);
        place(bpmSlider, 880, gridY, 184, controlHeight);
        place(meterSelect, 1072, gridY, 88, controlHeight);
        place(loopRangeButton, 1168, gridY, 92, controlHeight);
        place(repositorySelect, 395, 707, 450, 26);
        place(reuseFormulaButton, 855, 707, 105, 26);
        place(importCatalogButton, 970, 707, 140, 26);
        place(automationEditorButton, 1120, 707, 140, 26);
        place(status, 20, 845, 1144, 17);
        place(activityStatus, 20, 862, 684, 16);
        place(generationStatus, 704, 862, 460, 16);
        place(albumMenuButton, 1174, 846, 86, 31);
        place(seedTrackButton, 522, 9, 127, 30);
        place(playSongButton, 655, 11, 108, 26);
        place(stopButton, 769, 11, 62, 26);
        place(restartSongButton, 837, 11, 57, 26);
        place(recButton, 900, 11, 58, 26);
        place(languageSelect, 964, 11, 54, 26);
        place(tutorialButton, 1024, 11, 80, 26);
        place(learnButton, 1110, 11, 64, 26);
        place(aboutButton, 1186, 11, 74, 26);
        place(zoomOutButton, 750, 554, 32, 26);
        place(zoomInButton, 786, 554, 32, 26);
        place(zoomFitButton, 822, 554, 44, 26);
        place(undoGridButton, 620, 554, 100, 26);
        for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
            const int y = 586 + stem * 22;
            place(stemRoleSelect[static_cast<size_t>(stem)], 47, y + 1, 64, 19);
            place(stemGain[static_cast<size_t>(stem)], 1040, y + 3, 74, 16);
            place(stemMute[static_cast<size_t>(stem)], 1118, y + 1, 26, 19);
            place(stemSolo[static_cast<size_t>(stem)], 1148, y + 1, 26, 19);
            place(stemPan[static_cast<size_t>(stem)], 1180, y - 1, 26, 21);
            place(stemPanLabel[static_cast<size_t>(stem)], 1208, y + 1, 24, 18);
            place(stemFx[static_cast<size_t>(stem)], 1234, y + 1, 28, 19);
        }
        retroLookAndFeel.setUiScale(std::min(scaleX(), scaleY()));
        const auto uiScale = std::min(scaleX(), scaleY());
        status.setFont(juce::Font(juce::FontOptions(13.0f * uiScale)));
        for (auto* label : {&activityStatus, &generationStatus})
            label->setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 12.0f * uiScale,
                                                       juce::Font::plain)));
        const auto textFont = juce::Font(juce::FontOptions("DejaVu Sans Mono",
            15.0f * uiScale, juce::Font::plain));
        ascii.applyFontToAllText(juce::Font(juce::FontOptions("DejaVu Sans Mono",
            20.0f * uiScale, juce::Font::plain)));
        formulaInput.applyFontToAllText(juce::Font(juce::FontOptions("DejaVu Sans Mono",
            19.0f * uiScale, juce::Font::plain)));
        seedInput.applyFontToAllText(textFont);
        cellName.applyFontToAllText(textFont);
        startBeat.applyFontToAllText(textFont);
        formulaGridStatus.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono",
            13.0f * uiScale, juce::Font::bold)));
        for (auto& label : stemPanLabel)
            label.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono",
                std::max(10.0f,10.0f * uiScale), juce::Font::bold)));
        helpOverlay.setBounds(getLocalBounds());
        automationEditor.setBounds(juce::roundToInt(20.0f * scaleX()),
                                  juce::roundToInt(72.0f * scaleY()),
                                  juce::roundToInt(1240.0f * scaleX()),
                                  juce::roundToInt(780.0f * scaleY()));
        gestureEditor.setBounds(juce::roundToInt(20.0f * scaleX()),
                                juce::roundToInt(72.0f * scaleY()),
                                juce::roundToInt(1240.0f * scaleX()),
                                juce::roundToInt(780.0f * scaleY()));
    }

    void mouseDown(const juce::MouseEvent& event) override {
        const auto point = toLogical(event.getPosition());
        if (gridArea().contains(point)) {
            if (float(point.x) > gridTimelineRight()) return;
            grabKeyboardFocus();
            if (event.mods.isShiftDown()) {
                loopRangeSelecting = true;
                loopRangeAnchorBeat = beatAtGridX(point.x);
                loopRangeStartBeat = loopRangeAnchorBeat;
                loopRangeEndBeat = loopRangeStartBeat + 1;
                repaint();
                return;
            }
            const int placement = placementAtGridPoint(point, event.mods.isAltDown());
            if (placement >= 0) {
                beginPlacementDrag(placement, point);
            } else if (!event.mods.isAltDown()) {
                draggedPlacement = -1;
                handleGridClick(point);
            }
            return;
        }
        if (!drawingArea().contains(point)) return;
        if (event.mods.isAltDown()) {
            const auto area = drawingArea();
            const float nx = float(point.x - area.getX()) / float(area.getWidth());
            const float ny = float(point.y - area.getY()) / float(area.getHeight());
            float nearest = std::numeric_limits<float>::max();
            movingStroke = -1;
            for (int s = 0; s < inscription.count; ++s) {
                const auto& stroke = inscription.strokes[static_cast<size_t>(s)];
                for (int p = 0; p < stroke.count; ++p) {
                    const auto& candidate = stroke.points[static_cast<size_t>(p)];
                    const float distance = std::hypot(candidate.x - nx, candidate.y - ny);
                    if (distance < nearest) { nearest = distance; movingStroke = s; }
                }
            }
            if (nearest > 0.12f) movingStroke = -1;
            if (movingStroke >= 0) {
                lastStrokeMovePoint = point;
                movingStrokeHistoryPushed = false;
                currentPhraseGenerated = false;
            }
            return;
        }
        if (inscription.count >= int(inscription.strokes.size())) return;
        pushDrawingUndo();
        currentPhraseGenerated = false;
        auto& stroke = inscription.strokes[static_cast<size_t>(inscription.count++)];
        stroke = {};
        drawing = true;
        refreshDrawingHistoryButtons();
        drawingStartMillis = juce::Time::getMillisecondCounter();
        marqueeScrollStartMillis = drawingStartMillis;
        appendPoint(point, false, event.isPressureValid() ? event.pressure : -1.0f);
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        const auto point = toLogical(event.getPosition());
        if (movingStroke >= 0) {
            const auto area = drawingArea();
            const float dx = float(point.x - lastStrokeMovePoint.x) / float(area.getWidth());
            const float dy = float(point.y - lastStrokeMovePoint.y) / float(area.getHeight());
            if ((dx != 0.0f || dy != 0.0f) && !movingStrokeHistoryPushed) {
                pushDrawingUndo();
                movingStrokeHistoryPushed = true;
            }
            auto& stroke = inscription.strokes[static_cast<size_t>(movingStroke)];
            for (int p = 0; p < stroke.count; ++p) {
                auto& value = stroke.points[static_cast<size_t>(p)];
                value.x = std::clamp(value.x + dx, 0.0f, 1.0f);
                value.y = std::clamp(value.y + dy, 0.0f, 1.0f);
            }
            lastStrokeMovePoint = point;
            updateLiveDrawing(false);
            repaint();
            return;
        }
        if (loopRangeSelecting) {
            const int beat = beatAtGridX(point.x);
            loopRangeStartBeat = std::min(loopRangeAnchorBeat, double(beat));
            loopRangeEndBeat = std::max(loopRangeAnchorBeat, double(beat + 1));
            repaint();
            return;
        }
        if (draggedPlacement >= 0) {
            updatePlacementDrag(point);
            return;
        }
        if (drawing) appendPoint(point, false, event.isPressureValid() ? event.pressure : -1.0f);
    }
    void mouseMove(const juce::MouseEvent& event) override {
        const auto point = toLogical(event.getPosition());
        if (gridArea().contains(point) && float(point.x) <= gridTimelineRight()) {
            const int placement = placementAtGridPoint(point);
            const auto mode = gridDragModeAtPoint(placement, point);
            setMouseCursor(mode == GridDragMode::trimLeft || mode == GridDragMode::trimRight
                ? juce::MouseCursor::LeftRightResizeCursor
                : placement >= 0 ? juce::MouseCursor::DraggingHandCursor
                                 : juce::MouseCursor::PointingHandCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override {
        const auto point = toLogical(event.getPosition());
        if (!gridArea().contains(point) || float(point.x) > gridTimelineRight() ||
            wheel.deltaY == 0.0f) return;
        const int oldSpan = gridViewSpan();
        const double oldStart = gridViewStart();
        if (event.mods.isCtrlDown()) {
            const float anchorRatio = std::clamp((float(point.x) - gridTimelineLeft()) /
                std::max(1.0f, gridTimelineWidth()), 0.0f, 1.0f);
            const double anchorBeat = oldStart + double(anchorRatio) * double(oldSpan);
            const float factor = wheel.deltaY > 0.0f ? 0.8f : 1.25f;
            gridZoomBeats = std::clamp(int(std::round(float(oldSpan) * factor)),
                                       std::min(4, gridTotalBeats()), gridTotalBeats());
            gridScrollBeat = anchorBeat - double(anchorRatio) * double(gridZoomBeats);
        } else {
            const int step = std::max(1, song.beatsPerBar * 2);
            gridScrollBeat = oldStart + (wheel.deltaY > 0.0f ? -step : step);
        }
        gridScrollBeat = gridViewStart();
        repaint();
    }
    void mouseUp(const juce::MouseEvent& event) override {
        const auto point = toLogical(event.getPosition());
        if (movingStroke >= 0) {
            movingStroke = -1;
            status.setText(utf8("Traço deslocado · altura e percurso recalculados."), juce::dontSendNotification);
            repaint();
            return;
        }
        if (loopRangeSelecting) {
            const int beat = beatAtGridX(point.x);
            loopRangeStartBeat = std::clamp(std::min(loopRangeAnchorBeat, double(beat)), 0.0, 4095.0);
            loopRangeEndBeat = std::clamp(std::max(loopRangeAnchorBeat, double(beat + 1)),
                                          loopRangeStartBeat + 1.0, 4096.0);
            loopRangeSelecting = false;
            loopRangeButton.setToggleState(true, juce::dontSendNotification);
            loopRangeEnabled.store(true, std::memory_order_release);
            publishLoopRange();
            status.setText(utf8("Loop definido: compassos ") +
                juce::String(int(loopRangeStartBeat) / song.beatsPerBar + 1) + "–" +
                juce::String((int(loopRangeEndBeat) - 1) / song.beatsPerBar + 1) +
                utf8(". Use Loop trecho para ligar/desligar."), juce::dontSendNotification);
            repaint();
            return;
        }
        if (draggedPlacement >= 0) {
            finishPlacementDrag();
            return;
        }
        if (drawing) {
            appendPoint(point, true, event.isPressureValid() ? event.pressure : -1.0f);
            if (inscription.count > 0 && inscription.strokes[static_cast<size_t>(inscription.count - 1)].count < 2) {
                inscription.strokes[static_cast<size_t>(--inscription.count)] = {};
                if (drawingUndoCount > 0) {
                    drawingUndoHistory[static_cast<size_t>(--drawingUndoCount)] = {};
                    for (int i = 0; i < drawingRedoCount; ++i)
                        drawingRedoHistory[static_cast<size_t>(i)] = {};
                    drawingRedoCount = 0;
                }
            } else {
                const double duration = std::max(0.18, liveDuration.load(std::memory_order_relaxed));
                const double phase = std::fmod(std::max(0.0, playheadSeconds.load(std::memory_order_relaxed)) / duration, 1.0);
                releasedStrokeIndex = inscription.count - 1;
                releasedStrokeShift = float(phase * drawingArea().getWidth());
                releasedStrokeMillis = juce::Time::getMillisecondCounter();
                marqueeScrollStartMillis = releasedStrokeMillis;
            }
        }
        drawing = false;
        refreshDrawingHistoryButtons();
        repaint();
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr) return false;
        const bool command = key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown();
        if (command && (key.getKeyCode() == 's' || key.getKeyCode() == 'S')) {
            if (currentFile.existsAsFile() && !key.getModifiers().isShiftDown()) saveToFile(currentFile);
            else chooseSongFile(true);
            return true;
        }
        if (command && (key.getKeyCode() == 'o' || key.getKeyCode() == 'O')) { chooseSongFile(false); return true; }
        if (!command && key.getKeyCode() == juce::KeyPress::spaceKey) {
            if (playing.load(std::memory_order_acquire)) stopButton.triggerClick();
            else playArrangement();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::escapeKey && playing.load(std::memory_order_acquire)) {
            stopButton.triggerClick(); return true;
        }
        if ((key.getKeyCode() == 'z' || key.getKeyCode() == 'Z') &&
            (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()))
            return undoGridEdit();
        if (key.getKeyCode() == juce::KeyPress::deleteKey ||
            key.getKeyCode() == juce::KeyPress::backspaceKey) {
            if (selectedPlacement >= 0) {
                removeSelectedPlacement();
                return true;
            }
        }
        return false;
    }

private:
    static constexpr int logicalWidth = 1280;
    static constexpr int logicalHeight = 900;
    float scaleX() const noexcept { return float(getWidth()) / float(logicalWidth); }
    float scaleY() const noexcept { return float(getHeight()) / float(logicalHeight); }
    float autoEvolutionAmount() const noexcept {
        constexpr std::array<float, 4> amounts{{0.0f, 0.4f, 0.7f, 1.0f}};
        return amounts[static_cast<size_t>(std::clamp(autoEvolutionStrength, 1, 3))];
    }
    juce::Font canvasFont(juce::Font font) const {
        font.setTypefaceName("DejaVu Sans Mono");
        return font;
    }
    void drawCanvasText(juce::Graphics& g, const juce::String& text,
                        int x, int y, int width, int height,
                        juce::Justification justification, bool useEllipses = false) const {
        const float sx = scaleX(), sy = scaleY();
        g.saveState();
        g.addTransform(juce::AffineTransform::scale(1.0f / sx, 1.0f / sy));
        auto font = g.getCurrentFont();
        font.setHeight(font.getHeight() * sy);
        g.setFont(font);
        g.drawText(text, juce::roundToInt(float(x) * sx), juce::roundToInt(float(y) * sy),
                   juce::roundToInt(float(width) * sx), juce::roundToInt(float(height) * sy),
                   justification, useEllipses);
        g.restoreState();
    }
    void drawCanvasText(juce::Graphics& g, const juce::String& text,
                        juce::Rectangle<int> area, juce::Justification justification,
                        bool useEllipses = false) const {
        drawCanvasText(g, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                       justification, useEllipses);
    }
    juce::Point<int> toLogical(juce::Point<int> point) const {
        return {juce::roundToInt(float(point.x) / scaleX()),
                juce::roundToInt(float(point.y) / scaleY())};
    }
    template <typename ComponentType>
    void place(ComponentType& component, int x, int y, int width, int height) {
        const int slotX = juce::roundToInt(float(x) * scaleX());
        const int slotWidth = juce::roundToInt(float(width) * scaleX());
        int placedWidth = slotWidth;
        if constexpr (std::is_base_of_v<juce::Button, ComponentType> ||
                      std::is_base_of_v<juce::ComboBox, ComponentType>)
            placedWidth = juce::roundToInt(float(width) * std::min(scaleX(), scaleY()));
        component.setBounds(slotX + (slotWidth - placedWidth) / 2,
                            juce::roundToInt(float(y) * scaleY()),
                            placedWidth,
                            juce::roundToInt(float(height) * scaleY()));
    }

    juce::String uiText(const char* pt, const char* en, const char* fr, const char* es) const {
        const char* texts[] = {pt, en, fr, es};
        return utf8(texts[std::clamp(languageSelect.getSelectedId() - 1, 0, 3)]);
    }

    void updateInterfaceLanguage() {
        updateHelpLabels();
        automationEditorButton.setButtonText(uiText("CURVAS", "CURVES", "COURBES", "CURVAS"));
        albumMenuButton.setButtonText(uiText("Álbum…", "Album…", "Album…", "Álbum…"));
        cross.setButtonText(uiText("Riscar último", "Cross last", "Barrer dernier", "Tachar último"));
        undoDrawingButton.setButtonText(uiText("Desfazer", "Undo", "Annuler", "Deshacer"));
        redoDrawingButton.setButtonText(uiText("Refazer", "Redo", "Rétablir", "Rehacer"));
        clear.setButtonText(uiText("Limpar", "Clear", "Effacer", "Limpiar"));
        glyph.setButtonText(uiText("Leitura / glifo", "Reading / glyph", "Lecture / glyphe", "Lectura / glifo"));
        storeCellButton.setButtonText(uiText("Guardar célula", "Store cell", "Garder cellule", "Guardar célula"));
        updateCellButton.setButtonText(uiText("Atualizar", "Update", "Actualiser", "Actualizar"));
        placeCellButton.setButtonText(uiText("Inserir célula", "Place cell", "Placer cellule", "Insertar célula"));
        seedTrackButton.setButtonText(uiText("Faixa completa…", "Full track…", "Morceau complet…", "Pieza completa…"));
        restartSongButton.setButtonText(uiText("Início", "Restart", "Début", "Inicio"));
        undoGridButton.setButtonText(uiText("Desfazer", "Undo", "Annuler", "Deshacer"));
        saveButton.setButtonText(uiText("Salvar / WAV", "Save / WAV", "Sauver / WAV", "Guardar / WAV"));
        openButton.setButtonText(uiText("Abrir", "Open", "Ouvrir", "Abrir"));
        removeLastButton.setButtonText(uiText("Retirar célula", "Remove cell", "Retirer cellule", "Retirar célula"));
        applyFormula.setButtonText(uiText("Compilar fórmula", "Compile formula", "Compiler formule", "Compilar fórmula"));
        formulaToNotesButton.setButtonText(uiText("Fórmula → notas", "Formula → notes", "Formule → notes", "Fórmula → notas"));
        saveFormulaNotesButton.setButtonText(uiText("Guardar frase", "Store phrase", "Garder phrase", "Guardar frase"));
        reuseFormulaButton.setButtonText(uiText("Reusar", "Reuse", "Réutiliser", "Reutilizar"));
        importCatalogButton.setButtonText(uiText("Importar catálogo", "Import catalog", "Importer catalogue", "Importar catálogo"));
        varyPhraseButton.setButtonText(uiText("Variar frase", "Vary phrase", "Varier phrase", "Variar frase"));
        resetDirectButton.setButtonText(uiText("Reiniciar t", "Reset t", "Relancer t", "Reiniciar t"));
        stopButton.setButtonText(uiText("Parar", "Stop", "Arrêter", "Detener"));
        updateControlChoices();
        refreshInputButtonLabel();
        refreshGenerateButtonLabel();
        refreshToggleDisplays();
        refreshActivityLabel();
        refreshGenerationStatus();
        repaint();
    }

    static void setChoiceText(juce::ComboBox& box, int id, const juce::String& text) {
        const bool selected = box.getSelectedId() == id;
        box.changeItemText(id, text);
        if (selected) box.setSelectedId(id, juce::dontSendNotification);
    }

    void updateControlChoices() {
        setChoiceText(fontSelect, 1, uiText("Fonte: padrão", "Font: default", "Police : défaut", "Fuente: predet."));
        setChoiceText(fontSelect, 2, uiText("Fonte: terminal", "Font: terminal", "Police : terminal", "Fuente: terminal"));
        setChoiceText(fontSelect, 3, uiText("Fonte: geométrica", "Font: geometric", "Police : géométrique", "Fuente: geométrica"));
        setChoiceText(shapeSelect, 1, uiText("Shape: desligado", "Shape: off", "Shape : désactivé", "Shape: apagado"));
        setChoiceText(shapeSelect, 3, uiText("Shape: cúbica", "Shape: cubic", "Shape : cubique", "Shape: cúbica"));
        setChoiceText(shapeSelect, 4, uiText("Shape: dobra", "Shape: fold", "Shape : pliage", "Shape: pliegue"));
        setChoiceText(seedSource, 1, uiText("Número", "Number", "Nombre", "Número"));
        setChoiceText(seedSource, 2, uiText("Texto", "Text", "Texte", "Texto"));
        setChoiceText(seedSource, 3, uiText("Desenho", "Drawing", "Dessin", "Dibujo"));
        setChoiceText(seedSource, 4, uiText("Glitch: máscara", "Glitch: mask", "Glitch : masque", "Glitch: máscara"));
        setChoiceText(stemSelect, 4, uiText("Stem 4 · Ritmo", "Stem 4 · Rhythm", "Stem 4 · Rythme", "Stem 4 · Ritmo"));
        setChoiceText(stemSelect, 5, uiText("Stem 5 · Textura", "Stem 5 · Texture", "Stem 5 · Texture", "Stem 5 · Textura"));
        autoRouteButton.setButtonText(uiText("AUTO", "AUTO", "AUTO", "AUTO"));
        setChoiceText(stemSelect, 6, uiText("Sugerir posição", "Suggest placement", "Suggérer position", "Sugerir posición"));
        setChoiceText(stemSelect, 7, uiText("Equilibrar som", "Balance sound", "Équilibrer le son", "Equilibrar sonido"));
        setChoiceText(cellSpan, 1, uiText("1 tempo", "1 beat", "1 temps", "1 pulso"));
        setChoiceText(cellSpan, 2, uiText("1 compasso", "1 bar", "1 mesure", "1 compás"));
        setChoiceText(cellSpan, 4, uiText("2 compassos", "2 bars", "2 mesures", "2 compases"));
        setChoiceText(cellSpan, 3, uiText("4 compassos", "4 bars", "4 mesures", "4 compases"));
        setChoiceText(formulaMotionSelect, 1, uiText("Seguir", "Follow", "Suivre", "Seguir"));
        setChoiceText(formulaMotionSelect, 2, uiText("Contrapor", "Counter", "Opposer", "Contraponer"));
        setChoiceText(formulaMotionSelect, 3, uiText("Transfigurar", "Transfigure", "Transfigurer", "Transfigurar"));
        setChoiceText(formulaMotionSelect, 4, uiText("Encadear", "Link", "Enchaîner", "Encadenar"));
        setChoiceText(formulaMotionSelect, 5, uiText("Cadenciar", "Cadence", "Cadencer", "Cadenciar"));
        setChoiceText(formulaMotionSelect, 6, uiText("Acompanhar", "Accompany", "Accompagner", "Acompañar"));
        setChoiceText(formulaMotionSelect, 7, uiText("Guiar acorde", "Guide chords", "Guider accords", "Guiar acordes"));
        setChoiceText(responseModeSelect, 1, uiText("Resposta: desligada", "Response: off", "Réponse : désactivée", "Respuesta: apagada"));
        setChoiceText(formulaGroupingSelect, 1, uiText("Grupo: automático", "Group: automatic",
                                                       "Groupe : automatique", "Grupo: automático"));
        updateTimbreLabels();
        setChoiceText(formulaScaleSelect, 1, uiText("Escala: maior", "Scale: major", "Gamme : majeure", "Escala: mayor"));
        for (int count = 1; count <= repetitions.getNumItems(); ++count)
            setChoiceText(repetitions, count, juce::String(count) + (count == 1
                ? uiText(" vez", " repeat", " répétition", " vez")
                : uiText(" vezes", " repeats", " répétitions", " veces")));
        const char* englishNotes[] = {"C", "C♯/D♭", "D", "D♯/E♭", "E", "F", "F♯/G♭",
                                     "G", "G♯/A♭", "A", "A♯/B♭", "B"};
        const char* solfegeNotes[] = {"Do", "Do♯/Ré♭", "Ré", "Ré♯/Mi♭", "Mi", "Fa",
                                     "Fa♯/Sol♭", "Sol", "Sol♯/La♭", "La", "La♯/Si♭", "Si"};
        const char* portugueseNotes[] = {"Dó", "Dó♯/Ré♭", "Ré", "Ré♯/Mi♭", "Mi", "Fá",
                                        "Fá♯/Sol♭", "Sol", "Sol♯/Lá♭", "Lá", "Lá♯/Si♭", "Si"};
        for (int i = 0; i < 12; ++i)
            setChoiceText(formulaTonicSelect, i + 1, uiText("Tom: ", "Key: ", "Ton : ", "Tono: ")
                + uiText(portugueseNotes[i], englishNotes[i], solfegeNotes[i], solfegeNotes[i]));
        cellName.setTextToShowWhenEmpty(uiText("Nome da célula", "Cell name", "Nom de cellule", "Nombre de célula"),
                                        juce::Colours::grey);
        repositorySelect.setTextWhenNothingSelected(uiText("Repositório de funções", "Formula library",
                                                            "Bibliothèque de formules", "Biblioteca de fórmulas"));
    }

    void updateHelpLabels() {
        const int language = std::clamp(languageSelect.getSelectedId() - 1, 0, 3);
        const juce::String about[] = {utf8("Sobre"), "About", utf8("À propos"), utf8("Acerca de")};
        const juce::String tutorial[] = {"Tutorial", "Tutorial", utf8("Tutoriel"), "Tutorial"};
        const juce::String learn[] = {utf8("Aprender"), "Learn", utf8("Apprendre"), "Aprender"};
        aboutButton.setButtonText(about[language]);
        tutorialButton.setButtonText(tutorial[language]);
        learnButton.setButtonText(learn[language]);
        const char* stemRoles[4][6] = {
            {"Livre", "Pulso", "Baixo", "Harm.", "Melod.", "Text."},
            {"Free", "Pulse", "Bass", "Harm.", "Lead", "Text."},
            {"Libre", "Puls.", "Basse", "Harm.", "Mél.", "Text."},
            {"Libre", "Pulso", "Bajo", "Armon.", "Melod.", "Text."}
        };
        const char* cellRoles[4][6] = {
            {"livre", "pulso", "baixo", "harmonia", "melodia", "textura"},
            {"free", "pulse", "bass", "harmony", "melody", "texture"},
            {"libre", "pulsation", "basse", "harmonie", "mélodie", "texture"},
            {"libre", "pulso", "bajo", "armonía", "melodía", "textura"}
        };
        const juce::String stemRoleTips[4] = {
            utf8("Papel pretendido desta pista. A sugestão de posição favorece células com papel correspondente."),
            "Intended role for this lane. Placement suggestions favor cells with a matching role.",
            utf8("Rôle prévu pour cette piste. Les suggestions favorisent les cellules de même rôle."),
            utf8("Función prevista de esta pista. Las sugerencias favorecen las celdas con la misma función.")
        };
        for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
            auto& selector = stemRoleSelect[static_cast<size_t>(stem)];
            const int selectedRole = int(song.stemRole[static_cast<size_t>(stem)]) + 1;
            selector.clear(juce::dontSendNotification);
            for (int role = 0; role < 6; ++role)
                selector.addItem(utf8(stemRoles[language][role]), role + 1);
            selector.setSelectedId(selectedRole, juce::dontSendNotification);
            selector.setTooltip(stemRoleTips[language]);
        }
        for (int role = 0; role < 6; ++role)
            setChoiceText(roleSelect, role + 1, "P: " + utf8(cellRoles[language][role]));
        helpOverlay.setLanguage(languageSelect.getSelectedId());
    }

    void openHelp(int topic) {
        helpOverlay.setLanguage(languageSelect.getSelectedId());
        helpOverlay.setTopic(topic);
        helpOverlay.setVisible(true);
        helpOverlay.toFront(true);
    }

    void refreshMeterChoices() {
        meterSelect.clear(juce::dontSendNotification);
        for (int beats = 2; beats <= 7; ++beats)
            meterSelect.addItem(juce::String(beats) + "/4", beats);
        meterSelect.addItem("6/8", 8);
        for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
            juce::PopupMenu choices;
            const int selected = song.stemMeter[static_cast<size_t>(stem)];
            choices.addItem(100 + stem * 10, utf8("Seguir compasso da música"), true, selected == 0);
            for (int meter = 2; meter <= 8; ++meter)
                choices.addItem(100 + stem * 10 + meter,
                    meter == 8 ? juce::String("6/8") : juce::String(meter) + "/4", true, selected == meter);
            const auto label = selected == 0 ? utf8("seguir") : selected == 8 ? juce::String("6/8") : juce::String(selected) + "/4";
            meterSelect.getRootMenu()->addSubMenu("Stem " + juce::String(stem + 1) + utf8(" · ") + label, choices);
        }
        meterSelect.setSelectedId(song.compoundSixEight ? 8 : song.beatsPerBar, juce::dontSendNotification);
    }

    void updateTimbreLabels() {
        setChoiceText(timbreSelect, 1, uiText("Seno / função", "Sine / function", "Sinus / fonction", "Seno / función"));
        setChoiceText(timbreSelect, 3, uiText("AY · tons", "AY · tones", "AY · tons", "AY · tonos"));
        setChoiceText(timbreSelect, 4, uiText("AY · misto", "AY · mixed", "AY · mixte", "AY · mixto"));
        setChoiceText(timbreSelect, 5, uiText("AY · ruído", "AY · noise", "AY · bruit", "AY · ruido"));
        setChoiceText(timbreSelect, 6, uiText("AY-3-8910 · histórico", "AY-3-8910 · historical", "AY-3-8910 · historique", "AY-3-8910 · histórico"));
        setChoiceText(timbreSelect, 7, uiText("YM2149 · histórico", "YM2149 · historical", "YM2149 · historique", "YM2149 · histórico"));
    }

    void refreshTimbreChoices() {
        timbreSelect.clear(juce::dontSendNotification);
        timbreSelect.addItem(utf8("Seno / função"), 1);
        timbreSelect.addItem("Trio chip", 2);
        timbreSelect.addItem(utf8("AY · tons"), 3);
        timbreSelect.addItem(utf8("AY · misto"), 4);
        timbreSelect.addItem(utf8("AY · ruído"), 5);
        timbreSelect.addItem(utf8("AY-3-8910 · histórico"), 6);
        timbreSelect.addItem(utf8("YM2149 · histórico"), 7);
        updateTimbreLabels();
        juce::PopupMenu periods;
        for (int period : {1,2,4,8,16,31}) periods.addItem(100 + period, juce::String(period), true, song.ayNoisePeriod == period);
        timbreSelect.getRootMenu()->addSubMenu(utf8("AY · período do ruído: ") + juce::String(song.ayNoisePeriod), periods);
        const char* names[] = {"Risco · original", "Cair · uma vez", "Subir · sustentar", "Triângulo · repetir", "Cair · repetir"};
        juce::PopupMenu envelopes, ramps;
        for (int i = 0; i < 5; ++i) envelopes.addItem(300 + i, utf8(names[i]), true, song.ayEnvelopeMode == i);
        constexpr float times[] = {0.125f,0.25f,0.5f,1,2};
        for (int i = 0; i < 5; ++i) ramps.addItem(401 + i, juce::String(times[i],3) + utf8(" s · aprox."), true, song.ayEnvelopeRamp == times[i]);
        timbreSelect.getRootMenu()->addSubMenu(utf8("AY · envelope: ") + utf8(names[std::clamp(song.ayEnvelopeMode,0,4)]), envelopes);
        timbreSelect.getRootMenu()->addSubMenu(utf8("AY · rampa: ") + juce::String(song.ayEnvelopeRamp,3) + " s", ramps);
        const char* textureNames[] = {"Somente tons", "Tons + ruído", "Somente ruído"};
        juce::PopupMenu historicalTexture, historicalPeriods, historicalEnvelopes, historicalRamps;
        for (int i = 0; i < 3; ++i)
            historicalTexture.addItem(500 + i, utf8(textureNames[i]), true, song.historicalAyTexture == i);
        for (int period : {1,2,4,8,16,31})
            historicalPeriods.addItem(600 + period, juce::String(period), true, song.historicalAyNoisePeriod == period);
        const char* historicalEnvelopeNames[] = {"Volume fixo", "Cair uma vez", "Subir e sustentar", "Triângulo", "Cair e repetir"};
        for (int i = 0; i < 5; ++i)
            historicalEnvelopes.addItem(700 + i, utf8(historicalEnvelopeNames[i]), true, song.historicalAyEnvelope == i);
        for (int i = 0; i < 5; ++i)
            historicalRamps.addItem(801 + i, juce::String(times[i],3) + utf8(" s · aprox."), true,
                                    song.historicalAyEnvelopeRamp == times[i]);
        timbreSelect.getRootMenu()->addSubMenu(utf8("Histórico · mixer: ") +
            utf8(textureNames[std::clamp(song.historicalAyTexture,0,2)]), historicalTexture);
        timbreSelect.getRootMenu()->addSubMenu(utf8("Histórico · ruído: ") +
            juce::String(song.historicalAyNoisePeriod), historicalPeriods);
        timbreSelect.getRootMenu()->addSubMenu(utf8("Histórico · envelope: ") +
            utf8(historicalEnvelopeNames[std::clamp(song.historicalAyEnvelope,0,4)]), historicalEnvelopes);
        timbreSelect.getRootMenu()->addSubMenu(utf8("Histórico · rampa: ") +
            juce::String(song.historicalAyEnvelopeRamp,3) + " s", historicalRamps);
        timbreSelect.setSelectedId(song.timbreMode + 1, juce::dontSendNotification);
    }
    std::array<std::int16_t, risco::Cell::waveformSize> captureWaveform(
        const risco::Sequence& sequence) const noexcept {
        std::array<std::int16_t, risco::Cell::waveformSize> result{};
        if (!(sequence.end > 0.0)) return result;
        if (risco::isHistoricalAyTimbre(song.timbreMode)) {
            risco::HistoricalAyEngine engine;
            const double waveformRate = std::clamp(65536.0 / sequence.end, 8000.0, 48000.0);
            if (!engine.prepare(risco::historicalAyModel(song.timbreMode), waveformRate)) return result;
            std::uint64_t rendered = 0;
            for (int i = 0; i < risco::Cell::waveformSize; ++i) {
                const auto target = static_cast<std::uint64_t>(sequence.end * i * waveformRate /
                    double(risco::Cell::waveformSize));
                risco::HistoricalAyFrame frame;
                while (rendered <= target) {
                    frame = engine.process(sequence, double(rendered) / waveformRate,
                        {song.historicalAyTexture, song.historicalAyNoisePeriod,
                         song.historicalAyEnvelope, song.historicalAyEnvelopeRamp});
                    ++rendered;
                }
                const float sample = (frame.left + frame.right) * 0.5f;
                result[static_cast<size_t>(i)] = static_cast<std::int16_t>(
                    std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
            }
            return result;
        }
        for (int i = 0; i < risco::Cell::waveformSize; ++i) {
            const double time = sequence.end * i / double(risco::Cell::waveformSize);
            const auto mode = risco::noteWaveMode(song.timbreMode);
            const float sample = risco::sampleAt(sequence, time, mode, nullptr, song.bitDepth, song.ayNoisePeriod, song.ayEnvelopeMode, song.ayEnvelopeRamp);
            result[static_cast<size_t>(i)] = static_cast<std::int16_t>(
                std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        }
        return result;
    }

    std::array<std::int16_t, risco::Cell::waveformSize> captureFormulaWaveform(
        const risco::BytebeatProgram& program, std::uint32_t startT) const noexcept {
        std::array<std::int16_t, risco::Cell::waveformSize> result{};
        for (int i = 0; i < risco::Cell::waveformSize; ++i) {
            const float sample = risco::directBytebeatSample(&program, startT +
                static_cast<std::uint32_t>(i)) * 0.20f;
            result[static_cast<size_t>(i)] = static_cast<std::int16_t>(
                std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        }
        return result;
    }

    juce::File localFormulaFile() const {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Risco").getChildFile("formulas.riscolib");
    }

    bool writeLocalFormulaLibrary(const risco::FormulaLibrary& library) {
        std::ostringstream out;
        if (!risco::saveFormulaLibrary(out, library)) return false;
        const auto file = localFormulaFile();
        if (file.getParentDirectory().createDirectory().failed()) return false;
        juce::TemporaryFile temporary(file);
        const auto bytes = out.str();
        return temporary.getFile().replaceWithData(bytes.data(), bytes.size()) &&
               temporary.overwriteTargetFileWithTemporary();
    }

    void loadLocalFormulaLibrary() {
        const auto file = localFormulaFile();
        if (!file.existsAsFile()) {
            risco::FormulaLibrary initial;
            for (unsigned stage = 0; stage < 6; ++stage) {
                risco::FormulaRecord record;
                record.name = "Risco: estudo " + std::to_string(stage + 1);
                record.expression = risco::formulaFromSeedV3(20261114, stage);
                record.author = "Lúcio de Araújo / Risco";
                record.source = "risco:formula-machine-v3";
                record.sourceId = "seed=20261114;stage=" + std::to_string(stage);
                record.license = "AGPL-3.0-or-later";
                if (!risco::addFormula(initial, std::move(record))) return;
            }
            if (writeLocalFormulaLibrary(initial)) formulaLibrary = std::move(initial);
            else localLibraryWritable = false;
            return;
        }
        if (file.getSize() <= 0 || file.getSize() > 1024 * 1024) {
            localLibraryWritable = false;
            status.setText(utf8("Repositório de fórmulas fora do limite; arquivo preservado."),
                           juce::dontSendNotification);
            return;
        }
        juce::MemoryBlock bytes;
        if (!file.loadFileAsData(bytes)) { localLibraryWritable = false; return; }
        std::istringstream in(std::string(static_cast<const char*>(bytes.getData()), bytes.getSize()));
        risco::FormulaLibrary loaded;
        if (risco::loadFormulaLibrary(in, loaded)) formulaLibrary = std::move(loaded);
        else {
            localLibraryWritable = false;
            status.setText(utf8("Repositório de fórmulas inválido; arquivo preservado."),
                           juce::dontSendNotification);
        }
    }

    void refreshRepository() {
        const int previous = repositorySelect.getSelectedId();
        repositorySelect.clear(juce::dontSendNotification);
        for (int i = 0; i < formulaLibrary.count; ++i) {
            const auto& item = formulaLibrary.records[static_cast<size_t>(i)];
            repositorySelect.addItem(juce::String::fromUTF8(item.name.c_str()) + utf8(" · ") +
                                     juce::String::fromUTF8(item.author.c_str()), i + 1);
        }
        for (size_t i = 0; i < catalogCandidates.size(); ++i) {
            const auto& item = catalogCandidates[i];
            repositorySelect.addItem(utf8("Dollchan · ") + juce::String::fromUTF8(item.name.c_str()) +
                                     utf8(" · ") + juce::String::fromUTF8(item.author.c_str()),
                                     formulaLibrary.count + int(i) + 1);
        }
        if (previous > 0 && previous <= repositorySelect.getNumItems())
            repositorySelect.setSelectedId(previous, juce::dontSendNotification);
    }

    bool archiveFormula(const risco::Cell& cell) {
        if (!cell.bytebeatFormula || !localLibraryWritable) return false;
        risco::FormulaRecord record{cell.name, cell.formulaExpression,
                                    cell.formulaOrigin.author, cell.formulaOrigin.source,
                                    cell.formulaOrigin.sourceId,
                                    cell.formulaOrigin.license};
        for (int i = 0; i < formulaLibrary.count; ++i) {
            const auto& existing = formulaLibrary.records[static_cast<size_t>(i)];
            if (existing.expression == record.expression &&
                existing.sourceId == record.sourceId) return true;
        }
        auto next = formulaLibrary;
        if (!risco::addFormula(next, std::move(record)) || !writeLocalFormulaLibrary(next))
            return false;
        formulaLibrary = std::move(next);
        refreshRepository();
        repositorySelect.setSelectedId(formulaLibrary.count, juce::dontSendNotification);
        return true;
    }

    void reuseSelectedFormula() {
        const int selected = repositorySelect.getSelectedId() - 1;
        if (selected < 0) return;
        risco::FormulaRecord item;
        if (selected < formulaLibrary.count)
            item = formulaLibrary.records[static_cast<size_t>(selected)];
        else {
            const int external = selected - formulaLibrary.count;
            if (external >= int(catalogCandidates.size())) return;
            item = catalogCandidates[static_cast<size_t>(external)];
        }
        if (song.placementCount >= int(song.grid.size())) {
            status.setText(utf8("Grade cheia; não há espaço para reusar a fórmula."),
                           juce::dontSendNotification);
            return;
        }
        formulaInput.setText(juce::String::fromUTF8(item.expression.c_str()),
                             juce::dontSendNotification);
        cellName.setText(juce::String::fromUTF8(item.name.c_str()), juce::dontSendNotification);
        if (!compileAndApplyFormula()) return;
        timbreSelect.setSelectedId(1);
        directEnabled.store(true, std::memory_order_release);
        directMode.setToggleState(true, juce::dontSendNotification);
        resetDirect.store(true, std::memory_order_release);
        playing.store(true, std::memory_order_release);
        int start = 0;
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& p = song.grid[static_cast<size_t>(i)];
            start = std::max(start, p.startBeat + placementBeats(p));
        }
        if (start > 4096) {
            status.setText(utf8("A grade chegou ao limite de posição."), juce::dontSendNotification);
            return;
        }
        std::uint32_t id = 0;
        for (int i = 0; i < song.cellCount; ++i) {
            const auto& cell = song.bank[static_cast<size_t>(i)];
            if (cell.bytebeatFormula && cell.formulaExpression == item.expression &&
                cell.formulaOrigin.sourceId == item.sourceId) { id = cell.id; break; }
        }
        if (id == 0) {
            if (song.cellCount >= int(song.bank.size())) {
                status.setText(utf8("Banco cheio: reutilize uma fórmula já presente na música."),
                               juce::dontSendNotification);
                return;
            }
            const auto* program = activeFormula.load(std::memory_order_acquire);
            risco::FormulaOrigin origin{item.author, item.source, item.sourceId,
                                        0, 0, item.license};
            id = risco::storeFormulaCell(song, item.expression, item.name,
                captureFormulaWaveform(*program, 0), std::move(origin));
            if (id == 0) return;
            archiveFormula(song.bank[static_cast<size_t>(song.cellCount - 1)]);
            refreshBank(song.cellCount - 1, false);
        }
        risco::Placement reused{id, start, risco::CellSpan::fourBars, 1};
        reused.stem = pendingStem;
        const auto before = captureGridState();
        if (!risco::placeCell(song, reused)) return;
        pushGridUndo(before);
        status.setText(utf8("Fórmula reutilizada: ") + juce::String::fromUTF8(item.author.c_str()) +
                           utf8("; origem e onda preservadas."), juce::dontSendNotification);
        repaint();
    }

    void chooseFormulaCatalog() {
        fileChooser = std::make_unique<juce::FileChooser>(
            utf8("Importar índice all.gz do Dollchan"),
            juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.gz");
        juce::Component::SafePointer<MainComponent> safeThis(this);
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& chooser) {
                if (safeThis != nullptr && chooser.getResult() != juce::File())
                    safeThis->loadFormulaCatalog(chooser.getResult());
            });
    }

    void loadFormulaCatalog(const juce::File& file) {
        std::vector<risco::FormulaRecord> next;
        const auto result = risco::loadDollchanCatalog(file, next);
        if (result.failed()) {
            status.setText(result.getErrorMessage(), juce::dontSendNotification);
            return;
        }
        catalogCandidates = std::move(next);
        refreshRepository();
        status.setText(juce::String(catalogCandidates.size()) +
                           utf8(" fórmulas compatíveis listadas; escolha uma para reusar."),
                       juce::dontSendNotification);
    }

    bool appendFormulaToBank(risco::FormulaOrigin origin = {}, std::string name = {}) {
        const auto* program = activeFormula.load(std::memory_order_acquire);
        if (program == nullptr || song.cellCount >= int(song.bank.size()) ||
            song.placementCount >= int(song.grid.size())) {
            status.setText(utf8("Banco ou grade cheios; a fórmula atual continua audível."),
                           juce::dontSendNotification);
            return false;
        }
        int start = 0;
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& p = song.grid[static_cast<size_t>(i)];
            start = std::max(start, p.startBeat + placementBeats(p));
        }
        if (start > 4096) {
            status.setText(utf8("A grade chegou ao limite de posição."), juce::dontSendNotification);
            return false;
        }
        if (name.empty()) name = "Bytebeat " + std::to_string(song.cellCount + 1);
        const auto id = risco::storeFormulaCell(song, formulaInput.getText().toStdString(), std::move(name),
            captureFormulaWaveform(*program,
                resetDirect.load(std::memory_order_relaxed) ? 0u :
                displayedDirectTime.load(std::memory_order_relaxed)), std::move(origin));
        risco::Placement generated{id, start, risco::CellSpan::fourBars, 1};
        generated.stem = pendingStem;
        const auto gridBefore = captureGridState();
        if (id == 0 || !risco::placeCell(song, generated)) {
            status.setText(utf8("Não foi possível registrar a fórmula na performance."),
                           juce::dontSendNotification);
            return false;
        }
        pushGridUndo(gridBefore);
        ensurePlacementVisible(song.placementCount - 1);
        const bool archived = archiveFormula(song.bank[static_cast<size_t>(song.cellCount - 1)]);
        refreshBank(song.cellCount - 1, false);
        status.setText(archived
                           ? utf8("Fórmula guardada na performance · STEM ") + juce::String(generated.stem + 1) +
                                 utf8(" · também arquivada no repositório.")
                           : utf8("Fórmula guardada na performance · STEM ") + juce::String(generated.stem + 1) +
                                 utf8(" · repositório local cheio ou indisponível."),
                       juce::dontSendNotification);
        repaint();
        return true;
    }

    risco::PhraseMeters effectivePhraseMeters() const noexcept {
        return compareOriginalSections ? risco::PhraseMeters{} : currentPhraseMeters;
    }
    risco::PhraseSectionBars effectivePhraseSectionBars() const noexcept {
        return compareOriginalSections ? risco::PhraseSectionBars{{1,1,1,1}} : currentPhraseSectionBars;
    }
    double phraseLengthBeats() const noexcept {
        if (currentPhraseVersion < 6) return std::clamp(song.beatsPerBar, 2, 7) * (currentPhraseVersion == 4 ? 2.0 : 4.0);
        int beats = 0;
        for (int weight : risco::phraseSectionWeights(effectivePhraseMeters(), effectivePhraseSectionBars(), song.beatsPerBar)) beats += weight;
        return beats;
    }
    std::uint32_t phraseCycleMillis() const noexcept {
        const double beats = phraseLengthBeats();
        return static_cast<std::uint32_t>(std::max(1.0, std::round(
            beats * 60000.0 / std::clamp(song.bpm, 40.0, 240.0))));
    }

    std::uint32_t phraseCycleTicks() const noexcept {
        const double beats = phraseLengthBeats();
        return static_cast<std::uint32_t>(std::max(1.0, std::round(
            beats * 60.0 * risco::directBytebeatRate / std::clamp(song.bpm, 40.0, 240.0))));
    }

    risco::Sequence phraseForCurrentMeter(std::uint64_t seed, unsigned variation,
                                          unsigned version, unsigned sectionMask = 0x0f) const noexcept {
        auto source = risco::phraseFromSeed(seed, variation, version, song.bpm, sectionMask,
            song.beatsPerBar, song.compoundSixEight, effectivePhraseMeters());
        source = risco::stretchSections(source,
            version >= 6 ? risco::phraseMeterWeights(effectivePhraseMeters(), song.beatsPerBar)
                         : std::array<int,4>{{1,1,1,1}}, effectivePhraseSectionBars());
        const auto arranged = version >= 6 ? source : risco::arrange(source, {song.bpm, song.beatsPerBar,
                                      version == 4 ? risco::CellSpan::twoBars : risco::CellSpan::fourBars, 1});
        return risco::editSections(arranged, compareOriginalSections ? risco::SectionEdits{} : currentSectionEdits,
            version == 4 ? 2 : 4, version >= 6 ? risco::phraseSectionWeights(effectivePhraseMeters(), effectivePhraseSectionBars(), song.beatsPerBar) : effectivePhraseSectionBars());
    }

    juce::String activePhraseSectionsLabel() const {
        const std::array<const char*, 4> names = currentPhraseVersion == 8
            ? std::array<const char*, 4>{"A", "A′", "ápice", "retorno"}
            : currentPhraseVersion == 9
            ? std::array<const char*, 4>{"A", "B", "C", "A′"}
            : (currentPhraseVersion == 3 || currentPhraseVersion >= 5)
            ? std::array<const char*, 4>{"A1", "B", "A2", "C"}
            : currentPhraseVersion == 4
            ? std::array<const char*, 4>{"A", "B", "", ""}
            : std::array<const char*, 4>{"A", "A'", "contraste", "retorno"};
        juce::String active;
        const unsigned sectionCount = currentPhraseVersion == 4 ? 2u : 4u;
        for (unsigned section = 0; section < sectionCount; ++section) {
            if ((currentPhraseSectionMask & (1u << section)) == 0) continue;
            if (active.isNotEmpty()) active += "+";
            active += utf8(names[section]);
        }
        if (currentPhraseVersion >= 6) {
            active += utf8(" / métrica ");
            for (int i = 0; i < 4; ++i) {
                if (i > 0) active += "|";
                const int meter = effectivePhraseMeters()[static_cast<size_t>(i)];
                active += meter == 8 || (meter == 0 && song.compoundSixEight) ? "6/8" : juce::String(meter == 0 ? song.beatsPerBar : meter) + "/4";
                if (effectivePhraseSectionBars()[static_cast<size_t>(i)] > 1)
                    active += utf8("×") + juce::String(effectivePhraseSectionBars()[static_cast<size_t>(i)]);
            }
        }
        return utf8(" / seções ") + active;
    }

    juce::String phraseDynamicsLabel() const {
        const auto editing = compareOriginalSections ? utf8(" / comparação: original") : utf8(" / edições de seção aplicadas");
        if (currentPhraseVersion != 5) return editing;
        static const char* arcs[] = {"ápice tardio", "ápice antecipado", "crescendo longo"};
        return utf8(" / arco ") + utf8(arcs[currentPhraseVariation % 3]) + editing;
    }

    bool startFormulaEvolution() {
        const bool wasDirect = directEnabled.load(std::memory_order_acquire);
        if (!generateFormulaFromSeed()) {
            autoMode.setToggleState(false, juce::dontSendNotification);
            return false;
        }
        nextAutoTick = wasDirect
            ? displayedDirectTime.load(std::memory_order_relaxed) + phraseCycleTicks()
            : phraseCycleTicks();
        return true;
    }

    bool appendGeneratedPhrase() {
        if (!currentPhraseGenerated || song.placementCount >= int(song.grid.size())) return false;
        if (song.cellCount >= int(song.bank.size())) {
            status.setText(utf8("Banco cheio: retire uma célula para continuar gravando."),
                           juce::dontSendNotification);
            recButton.setToggleState(false, juce::dontSendNotification);
            return false;
        }
        const auto sequence = phraseForCurrentMeter(
            currentPhraseSeed, currentPhraseVariation, currentPhraseVersion, currentPhraseSectionMask);
        int start = 0;
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& p = song.grid[static_cast<size_t>(i)];
            start = std::max(start, p.startBeat + placementBeats(p));
        }
        const auto name = "Frase V" + std::to_string(currentPhraseVersion) + " #" +
                          std::to_string(song.cellCount + 1);
        const auto id = risco::storeGeneratedCell(song, currentPhraseSeed,
            currentPhraseVariation, name, captureWaveform(sequence), currentPhraseVersion,
            currentPhraseSectionMask);
        if (id != 0) song.bank[static_cast<size_t>(song.cellCount - 1)].sectionEdits =
            compareOriginalSections ? risco::SectionEdits{} : currentSectionEdits;
        if (id != 0) song.bank[static_cast<size_t>(song.cellCount - 1)].phraseMeters = effectivePhraseMeters();
        if (id != 0) song.bank[static_cast<size_t>(song.cellCount - 1)].phraseSectionBars = effectivePhraseSectionBars();
        const auto span = currentPhraseVersion == 4 ? risco::CellSpan::twoBars : risco::CellSpan::fourBars;
        int generatedStart = start;
        int generatedStem = pendingStem;
        if (autoRouteButton.getToggleState()) {
            const auto routeSeed = currentPhraseSeed ^
                (std::uint64_t(currentPhraseVariation) * 0x9e3779b97f4a7c15ULL);
            if (id != 0)
                song.bank[static_cast<size_t>(song.cellCount - 1)].role = generativeRoleForCell(
                    song.bank[static_cast<size_t>(song.cellCount - 1)], routeSeed);
            const auto suggestion = risco::suggestPlacement(song, id, span, 1,
                start, true, routeSeed);
            if (suggestion.valid) {
                generatedStart = suggestion.startBeat;
                generatedStem = suggestion.stem;
            }
        }
        risco::Placement generated{id, generatedStart, span, 1};
        generated.stem = generatedStem;
        const auto gridBefore = captureGridState();
        if (id == 0 || !risco::placeCell(song, generated)) {
            status.setText(utf8("Não foi possível registrar a função na performance."),
                           juce::dontSendNotification);
            return false;
        }
        pushGridUndo(gridBefore);
        ensurePlacementVisible(song.placementCount - 1);
        refreshBank(song.cellCount - 1, false);
        playArrangement(false);
        status.setText((autoRouteButton.getToggleState()
            ? utf8("Risco escolheu a entrada desta frase · STEM ")
            : utf8("Função registrada no banco e na grade, STEM ")) +
                       juce::String(generated.stem + 1) +
                       (autoRouteButton.getToggleState() ? utf8(" · seed + atividade da grade.") : "."),
                       juce::dontSendNotification);
        repaint();
        return true;
    }

    bool compileAndApplyFormula() {
        if (formulaSnapshots.size() >= 128) {
            status.setText(utf8("Limite de fórmulas nesta sessão; reinicie a maquete."),
                           juce::dontSendNotification);
            return false;
        }
        auto program = std::make_unique<risco::BytebeatProgram>();
        std::string error;
        if (!risco::compileBytebeat(formulaInput.getText().toStdString(), *program, error)) {
            status.setText(juce::String::fromUTF8(error.c_str()), juce::dontSendNotification);
            return false;
        }
        const auto* pointer = program.get();
        formulaSnapshots.push_back(std::move(program));
        activeFormula.store(pointer, std::memory_order_release);
        compiledFormulaExpression = formulaInput.getText().toStdString();
        functionMode.setToggleState(true, juce::dontSendNotification);
        formulaEnabled.store(true, std::memory_order_release);
        status.setText(pointer->floating ? utf8("Floatbeat compilado · t em segundos; saída -1 a 1; 8 kHz. Ouvir fórmula para áudio direto.") :
                       utf8("Bytebeat compilado · t é o contador de amostras de 8 kHz."),
                       juce::dontSendNotification);
        return true;
    }

    void refreshMetricGroupingOptions() {
        const int beats = song.beatsPerBar;
        const int count = risco::formulaMetricGroupingCount(beats);
        if (song.formulaMetricGrouping >= count) song.formulaMetricGrouping = -1;
        formulaGroupingSelect.clear(juce::dontSendNotification);
        formulaGroupingSelect.addItem(uiText("Grupo: automático", "Group: automatic", "Groupe : automatique", "Grupo: automático"), 1);
        if (beats >= 5) {
            for (int variant = 0; variant < count; ++variant) {
                const auto label = juce::String(beats) + "/4 · " +
                    juce::String::fromUTF8(risco::formulaMetricGrouping(beats, variant));
                formulaGroupingSelect.addItem(label, variant + 2);
            }
        }
        formulaGroupingSelect.setSelectedId(song.formulaMetricGrouping < 0
            ? 1 : song.formulaMetricGrouping + 2, juce::dontSendNotification);
    }

    juce::String harmonicPlanLabel() const {
        const char* names[] = {"Retorno", "Ciclo", "Pedal"};
        return juce::String(names[std::clamp(formulaProgressionChoice, 0, 2)]) + " / " +
            juce::String(formulaHarmonicBarsChoice) + utf8(" comp./acorde") +
            (std::any_of(formulaChordRootChoices.begin(), formulaChordRootChoices.end(), [](int root) { return root != -1; })
                ? utf8(" / harmonia por compasso editada") : juce::String());
    }
    juce::String swingLabel() const {
        if (formulaPulsesChoice > 0) return " / " + juce::String(formulaPulsesChoice) + utf8(" pulsos/compasso · swing/síncope suspensos");
        const int first = int(std::lround(50.0 * (1.0 + formulaSwingChoice)));
        return " / swing " + juce::String(first) + ":" + juce::String(100 - first) +
            (formulaSyncopationChoice ? utf8(" / síncope ON") : juce::String());
    }
    void previewFormulaAsNotes() {
        if (snapshots.size() >= 1024) {
            status.setText(utf8("Limite de frases nesta sessão; reinicie a maquete."),
                           juce::dontSendNotification);
            return;
        }
        if (normalizeFormulaText(formulaInput.getText().toStdString()) !=
                normalizeFormulaText(compiledFormulaExpression) ||
            activeFormula.load(std::memory_order_acquire) == nullptr) {
            if (!compileAndApplyFormula()) return;
        }
        const auto* formula = activeFormula.load(std::memory_order_acquire);
        if (formula == nullptr || !formula->valid) return;
        const auto scalePreset = static_cast<risco::FormulaScalePreset>(
            std::clamp(formulaScaleSelect.getSelectedId() - 1, 0, 26));
        const auto scaleMotion = static_cast<risco::FormulaScaleMotion>(
            std::clamp(formulaMotionSelect.getSelectedId() - 1, 0, 6));
        const int tonicSemitones = std::clamp(formulaTonicSelect.getSelectedId() - 1, 0, 11);
        const auto mapped = risco::composeFormulaEvents(*formula, bpmSlider.getValue(),
            song.beatsPerBar, scalePreset, scaleMotion, tonicSemitones,
            song.formulaMetricGrouping, formulaProgressionChoice, formulaHarmonicBarsChoice, formulaSwingChoice, formulaSubdivisionChoice, song.compoundSixEight, formulaChordRootChoices, formulaSyncopationChoice, formulaPulsesChoice);
        if (mapped.sequence.count == 0) {
            status.setText(utf8("A fórmula produziu somente pausas nessa leitura; ajuste a fórmula e tente novamente."),
                           juce::dontSendNotification);
            return;
        }
        auto next = std::make_unique<risco::Sequence>(mapped.sequence);
        const auto* sequence = next.get();
        snapshots.push_back(std::move(next));
        directEnabled.store(false, std::memory_order_release);
        directMode.setToggleState(false, juce::dontSendNotification);
        autoMode.setToggleState(false, juce::dontSendNotification);
        formulaEnabled.store(false, std::memory_order_release);
        functionMode.setToggleState(false, juce::dontSendNotification);
        timbreSelect.setSelectedId(1);
        activeSong.store(nullptr, std::memory_order_release);
        previewingCell = false;
        active.store(sequence, std::memory_order_release);
        liveDuration.store(sequence->end, std::memory_order_relaxed);
        loopEnabled.store(false, std::memory_order_release);
        loopMode.setToggleState(false, juce::dontSendNotification);
        restart.store(true, std::memory_order_release);
        playing.store(true, std::memory_order_release);
        status.setText(utf8("Fórmula → notas: ") + juce::String(mapped.sequence.count) +
                           utf8(" eventos, ") + juce::String(mapped.rests) +
                           utf8(" pausas, ") + juce::String(mapped.bars) + utf8(" compassos · ") +
                           juce::String::fromUTF8(risco::formulaScaleName(scalePreset)) + utf8(" / ") +
                           formulaTonicSelect.getText() + utf8(" / ") +
                           juce::String::fromUTF8(risco::formulaScaleMotionName(scaleMotion)) +
                           utf8(" · grupo ") + juce::String::fromUTF8(
                               song.compoundSixEight ? "3+3 (6/8)" : risco::formulaMetricGrouping(song.beatsPerBar, mapped.groupingVariant)) +
                           (scaleMotion == risco::FormulaScaleMotion::accompany || scaleMotion == risco::FormulaScaleMotion::chordGuide
                               ? utf8(" · ") + harmonicPlanLabel()
                               : utf8(" · ") + juce::String(mapped.machineSections) + utf8(" passagens de compasso.")) + swingLabel(),
                       juce::dontSendNotification);
    }

    void saveFormulaEventsToArrangement() {
        if (normalizeFormulaText(formulaInput.getText().toStdString()) !=
                normalizeFormulaText(compiledFormulaExpression) ||
            activeFormula.load(std::memory_order_acquire) == nullptr) {
            if (!compileAndApplyFormula()) return;
        }
        const auto* formula = activeFormula.load(std::memory_order_acquire);
        if (formula == nullptr || !formula->valid) return;
        if (song.cellCount >= int(song.bank.size()) || song.placementCount >= int(song.grid.size())) {
            status.setText(utf8("Banco ou grade cheios; libere espaço antes de guardar a frase."),
                           juce::dontSendNotification);
            return;
        }
        const auto scale = static_cast<risco::FormulaScalePreset>(
            std::clamp(formulaScaleSelect.getSelectedId() - 1, 0, 26));
        const auto motion = static_cast<risco::FormulaScaleMotion>(
            std::clamp(formulaMotionSelect.getSelectedId() - 1, 0, 6));
        const int tonicSemitones = std::clamp(formulaTonicSelect.getSelectedId() - 1, 0, 11);
        const double bpm = std::clamp(bpmSlider.getValue(), 40.0, 240.0);
        const int beatsPerBar = song.beatsPerBar;
        const auto mapped = risco::composeFormulaEvents(*formula, bpm, beatsPerBar,
            scale, motion, tonicSemitones, song.formulaMetricGrouping,
            formulaProgressionChoice, formulaHarmonicBarsChoice, formulaSwingChoice, formulaSubdivisionChoice, song.compoundSixEight, formulaChordRootChoices, formulaSyncopationChoice, formulaPulsesChoice);
        if (mapped.sequence.count == 0) {
            status.setText(utf8("A fórmula produziu somente pausas; ajuste a fórmula e tente novamente."),
                           juce::dontSendNotification);
            return;
        }
        int start = 0;
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& p = song.grid[static_cast<size_t>(i)];
            start = std::max(start, p.startBeat + placementBeats(p));
        }
        const int phraseBeats = mapped.bars * beatsPerBar;
        if (start + phraseBeats > 4096) {
            status.setText(utf8("A frase ultrapassaria o limite de posição da grade."),
                           juce::dontSendNotification);
            return;
        }
        std::string name = cellName.getText().trim().toStdString();
        if (name.empty())
            name = uiText("Frase fórmula ", "Formula phrase ", "Phrase formule ",
                          "Frase de fórmula ").toStdString() + std::to_string(song.cellCount + 1);
        const auto id = risco::storeFormulaEventCell(song, formulaInput.getText().toStdString(), name,
            captureFormulaWaveform(*formula, 0), mapped.sequence, scale, motion,
            tonicSemitones, bpm, {}, formulaProgressionChoice, formulaHarmonicBarsChoice, formulaSwingChoice, formulaSubdivisionChoice, formulaChordRootChoices, formulaSyncopationChoice, formulaPulsesChoice);
        if (id == 0) {
            status.setText(utf8("Não foi possível guardar a frase; revise nome e fórmula."),
                           juce::dontSendNotification);
            return;
        }
        risco::Placement placement{id, start, risco::CellSpan::fourBars, 1};
        placement.lengthBeats = phraseBeats;
        placement.stem = pendingStem;
        const auto before = captureGridState();
        if (!risco::placeCell(song, placement)) {
            --song.cellCount;
            --song.nextCellId;
            status.setText(utf8("A frase não pôde ser colocada na grade."), juce::dontSendNotification);
            return;
        }
        pushGridUndo(before);
        refreshBank(song.cellCount - 1, false);
        selectedPlacement = song.placementCount - 1;
        ensurePlacementVisible(selectedPlacement);
        startBeat.setText(juce::String(start + phraseBeats));
        refreshPlaybackAfterGridEdit();
        status.setText(uiText("Frase guardada e inserida no STEM ", "Phrase saved and placed on STEM ",
                             "Phrase enregistrée et placée sur STEM ", "Frase guardada e insertada en STEM ")
            + juce::String(pendingStem + 1)
            + uiText(" · grupo ", " · group ", " · groupe ", " · grupo ") + juce::String::fromUTF8(
                risco::formulaMetricGrouping(beatsPerBar, mapped.groupingVariant)) +
            uiText(" · fórmula, escala e gesto preservados.", " · formula, scale and gesture kept.",
                   " · formule, gamme et geste conservés.", " · fórmula, escala y gesto conservados."),
            juce::dontSendNotification);
        repaint();
    }

    bool generatePhraseFromSeed(unsigned variation, bool forcePreview = false) {
        if (snapshots.size() >= 1024) {
            status.setText(utf8("Limite de frases nesta sessão; reinicie a maquete."),
                           juce::dontSendNotification);
            return false;
        }
        std::uint64_t seed = currentPhraseGenerated ? currentPhraseSeed : 0;
        const int source = seedSource.getSelectedId();
        if (currentPhraseGenerated) {
            // Continue the current generative identity, including a bank cell.
        } else if (source == 1) {
            const auto value = seedInput.getText().toStdString();
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seed);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                return false;
        } else if (source == 2) {
            const auto value = ascii.getText().toStdString();
            if (value.empty()) return false;
            seed = risco::formulaSeedFromText(value);
        } else if (source == 3) {
            bool usable = false;
            for (int i = 0; i < std::clamp(inscription.count, 0, int(inscription.strokes.size())); ++i)
                usable = usable || inscription.strokes[static_cast<size_t>(i)].count >= 2;
            if (!usable) return false;
            seed = risco::formulaSeedFromDrawing(inscription);
        } else return false;
        if (!currentPhraseGenerated) {
            currentPhraseVersion = selectedPhraseVersion;
            currentPhraseSectionMask = 0x0f;
            currentSectionEdits = {};
            currentPhraseMeters = {};
            currentPhraseSectionBars = {{1,1,1,1}};
            compareOriginalSections = false;
        }
        auto next = std::make_unique<risco::Sequence>(
            phraseForCurrentMeter(seed, variation, currentPhraseVersion, currentPhraseSectionMask));
        const auto* pointer = next.get();
        snapshots.push_back(std::move(next));
        currentPhraseGenerated = true;
        currentPhraseSeed = seed;
        currentPhraseVariation = variation;
        const bool accumulating = !forcePreview && (recButton.getToggleState() || autoPhraseButton.getToggleState());
        directEnabled.store(false, std::memory_order_release);
        directMode.setToggleState(false, juce::dontSendNotification);
        formulaEnabled.store(false, std::memory_order_release);
        functionMode.setToggleState(false, juce::dontSendNotification);
        if (!accumulating) {
            activeSong.store(nullptr, std::memory_order_release);
            previewingCell = false;
            active.store(pointer, std::memory_order_release);
            liveDuration.store(pointer->end, std::memory_order_relaxed);
            loopEnabled.store(true, std::memory_order_release);
            loopMode.setToggleState(true, juce::dontSendNotification);
            restart.store(true, std::memory_order_release);
            playing.store(true, std::memory_order_release);
        }
        status.setText(utf8("Frase generativa ativa — seed ") + juce::String(seed) +
                           utf8(" / variação ") + juce::String(variation + 1) +
                           utf8(" / harmonia ") + juce::String(variation % 4 + 1) +
                           activePhraseSectionsLabel() + phraseDynamicsLabel(),
                       juce::dontSendNotification);
        if (autoPhraseButton.getToggleState())
            nextPhraseMillis = juce::Time::getMillisecondCounter() + phraseCycleMillis();
        return true;
    }

    bool generateFormulaFromSeed() {
        if (song.placementCount >= int(song.grid.size())) {
            status.setText(utf8("Grade cheia; salve a música antes de continuar."),
                           juce::dontSendNotification);
            return false;
        }
        const int source = seedSource.getSelectedId();
        std::uint64_t seed = 0;
        if (source == 1) {
            const auto value = seedInput.getText().toStdString();
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seed);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                status.setText(utf8("Digite um seed decimal válido."), juce::dontSendNotification);
                return false;
            }
        } else if (source == 2) {
            const auto value = ascii.getText().toStdString();
            if (value.empty()) {
                status.setText(utf8("Digite texto para gerar a função."),
                               juce::dontSendNotification);
                return false;
            }
            seed = risco::formulaSeedFromText(value);
        } else if (source == 3) {
            bool usable = false;
            for (int i = 0; i < std::clamp(inscription.count, 0, int(inscription.strokes.size())); ++i)
                usable = usable || inscription.strokes[static_cast<size_t>(i)].count >= 2;
            if (!usable) {
                status.setText(utf8("Desenhe um traço com ao menos dois pontos."),
                               juce::dontSendNotification);
                return false;
            }
            seed = risco::formulaSeedFromDrawing(inscription);
        } else return false;
        if (!hasLastSeed || seed != lastSeed || source != lastSeedSource) {
            generationIndex = 0;
            lastSeed = seed;
            lastSeedSource = source;
            hasLastSeed = true;
        }
        const auto phase = static_cast<unsigned>(generationIndex % 9);
        const auto seriesSeed = seed + static_cast<std::uint64_t>(generationIndex);
        std::vector<const risco::Cell*> previous;
        for (int i = 0; i < song.cellCount; ++i) {
            const auto& cell = song.bank[static_cast<size_t>(i)];
            if (cell.bytebeatFormula) previous.push_back(&cell);
        }
        if ((phase == 6 || song.cellCount >= int(song.bank.size())) && !previous.empty()) {
            const auto* recalled = previous[static_cast<size_t>((seed + generationIndex) % previous.size())];
            if (!replayFormulaCell(*recalled)) return false;
            ++generationIndex;
            status.setText(utf8("Origem ") + juce::String(seed) + utf8(" · seed atual ") +
                               juce::String(seriesSeed) + utf8(" · retomada: ") +
                               juce::String::fromUTF8(recalled->name.c_str()),
                           juce::dontSendNotification);
            return true;
        }
        risco::FormulaOrigin origin;
        origin.author = "Lúcio de Araújo / Risco";
        origin.license = "AGPL-3.0-or-later";
        std::string name;
        std::string formula;
        if (phase == 7 && previous.size() >= 2) {
            const risco::Cell* first = nullptr;
            const risco::Cell* second = nullptr;
            for (auto it = previous.rbegin(); it != previous.rend(); ++it) {
                if ((*it)->formulaExpression.size() > 58) continue;
                if (first == nullptr) first = *it;
                else { second = *it; break; }
            }
            if (first != nullptr && second != nullptr) {
                const auto recombinationSeed = seed + generationIndex;
                const auto operation = risco::recombinationOperatorV2(recombinationSeed);
                formula = risco::recombineFormulaV2(first->formulaExpression,
                                                    second->formulaExpression,
                                                    recombinationSeed);
                if (!formula.empty()) {
                    origin.parentA = first->id;
                    origin.parentB = second->id;
                    origin.source = "risco:recombine-v2";
                    const auto combinedAuthors = std::string("Lúcio de Araújo / Risco; ") +
                        first->formulaOrigin.author + "; " + second->formulaOrigin.author;
                    if (combinedAuthors.size() <= 128) origin.author = combinedAuthors;
                    if (first->formulaOrigin.license != "AGPL-3.0-or-later" ||
                        second->formulaOrigin.license != "AGPL-3.0-or-later") {
                        origin.license = "rights-unverified";
                        origin.source = "risco:recombine-v2;" +
                            first->formulaOrigin.sourceId + ";" +
                            second->formulaOrigin.sourceId;
                    }
                    origin.sourceId = "parents=" + std::to_string(first->id) + "," +
                        std::to_string(second->id) + ";op=" + operation +
                        ";step=" + std::to_string(generationIndex);
                    name = "Risco recombina " + std::to_string(first->id) +
                           operation + std::to_string(second->id);
                }
            }
        }
        if (phase == 8 && !previous.empty()) {
            const auto firstCandidate = static_cast<size_t>(
                (seed + generationIndex) % previous.size());
            for (size_t attempt = 0; attempt < previous.size() && formula.empty(); ++attempt) {
                const auto* parent = previous[(firstCandidate + attempt) % previous.size()];
                const auto glitch = risco::glitchFormulaV1(
                    parent->formulaExpression, seed + generationIndex + attempt);
                if (glitch.expression.empty()) continue;
                formula = glitch.expression;
                origin.parentA = parent->id;
                origin.source = "risco:glitch-math-v1";
                origin.license = parent->formulaOrigin.license == "AGPL-3.0-or-later"
                    ? "AGPL-3.0-or-later" : "rights-unverified";
                const auto derivedAuthor = std::string("Lúcio de Araújo / Risco; ") +
                    parent->formulaOrigin.author;
                if (derivedAuthor.size() <= 128) origin.author = derivedAuthor;
                origin.sourceId = "parent=" + std::to_string(parent->id) +
                    ";kind=" + glitch.kind + ";a=" +
                    std::to_string(glitch.parameterA) + ";b=" +
                    std::to_string(glitch.parameterB) + ";step=" +
                    std::to_string(generationIndex);
                name = "Risco glitch " + std::to_string(parent->id) + " " +
                    glitch.kind + " " + std::to_string(glitch.parameterA);
            }
        }
        if (formula.empty()) {
            const unsigned stage = phase < 6 ? phase : 5;
            formula = risco::formulaFromSeedV3(seriesSeed, stage);
            origin.source = "risco:formula-machine-v3";
            origin.sourceId = "seed=" + std::to_string(seriesSeed) + ";stage=" +
                              std::to_string(stage);
            name = "Risco calculo " + std::to_string(stage + 1);
        }
        formulaInput.setText(juce::String::fromUTF8(formula.c_str()), juce::dontSendNotification);
        if (!compileAndApplyFormula()) return false;
        if (!directEnabled.load(std::memory_order_acquire)) {
            directEnabled.store(true, std::memory_order_release);
            directMode.setToggleState(true, juce::dontSendNotification);
            resetDirect.store(true, std::memory_order_release);
            nextAutoTick = phraseCycleTicks();
            playing.store(true, std::memory_order_release);
        }
        if (!appendFormulaToBank(std::move(origin), std::move(name))) return false;
        ++generationIndex;
        status.setText(utf8("Origem ") + juce::String(seed) + utf8(" · seed atual ") +
                           juce::String(seriesSeed) + utf8(" · passo ") +
                           juce::String(generationIndex) + utf8(": função criada e arquivada no STEM ") +
                           juce::String(pendingStem + 1) + ".",
                       juce::dontSendNotification);
        return true;
    }

    void refreshGenerateButtonLabel() {
        generateFormulaButton.setButtonText(manualGlitchMode
            ? uiText("Aplicar glitch", "Apply glitch", "Appliquer glitch", "Aplicar glitch")
            : uiText("Gerar + tocar", "Generate + play", "Générer + jouer", "Generar + tocar"));
    }

    void updateGlitchControls() {
        const bool glitchMode = seedSource.getSelectedId() >= 4;
        if (glitchMode && !manualGlitchMode) {
            seedBeforeGlitch = seedInput.getText().toStdString();
            seedInput.setInputRestrictions(3, "0123456789");
            seedInput.setText("50", juce::dontSendNotification);
        } else if (!glitchMode && manualGlitchMode) {
            seedInput.setInputRestrictions(20, "0123456789");
            seedInput.setText(juce::String(seedBeforeGlitch), juce::dontSendNotification);
        }
        manualGlitchMode = glitchMode;
        autoMode.setEnabled(!glitchMode);
        refreshGenerateButtonLabel();
        if (glitchMode) {
            generateFormulaButton.setTooltip(utf8("Aplica o tipo selecionado à fórmula Bytebeat da célula escolhida. Intensidade de 1 a 100; cria e insere uma nova célula sem alterar o pai."));
            seedInput.setTooltip(utf8("Intensidade do glitch, de 1 a 100. Selecione antes a fórmula Bytebeat-pai no banco de células."));
            autoMode.setToggleState(false, juce::dontSendNotification);
        } else {
            generateFormulaButton.setTooltip(utf8("Cada fórmula gerada vai para o banco e para a grade; o repositório local de fórmulas persiste entre músicas."));
            seedInput.setTooltip(utf8("Um seed de origem. Texto/Desenho usam seus dados; Bytebeat e Frases derivam variações em contadores separados."));
        }
    }

    bool applyManualGlitch() {
        const int parentIndex = selectedCellIndex();
        if (parentIndex < 0 || !song.bank[static_cast<size_t>(parentIndex)].bytebeatFormula) {
            status.setText(utf8("Selecione no banco uma célula com fórmula Bytebeat para ser o pai."),
                           juce::dontSendNotification);
            return false;
        }
        unsigned intensity = 0;
        const auto value = seedInput.getText().toStdString();
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), intensity);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            intensity < 1 || intensity > 100) {
            status.setText(utf8("A intensidade deve ser um número de 1 a 100."),
                           juce::dontSendNotification);
            return false;
        }
        const auto& parent = song.bank[static_cast<size_t>(parentIndex)];
        const auto kind = seedSource.getSelectedId() == 4 ? risco::FormulaGlitchKind::mask :
            seedSource.getSelectedId() == 5 ? risco::FormulaGlitchKind::xorPulse :
                                                risco::FormulaGlitchKind::wrap;
        const auto seed = static_cast<std::uint64_t>(parent.id) * 0x9e3779b97f4a7c15ULL +
                          static_cast<std::uint64_t>(song.cellCount + 1);
        const auto glitch = risco::glitchFormulaV1(parent.formulaExpression, seed, kind, intensity);
        if (glitch.expression.empty()) {
            status.setText(utf8("Não foi possível compilar este glitch; tente outra fórmula-pai."),
                           juce::dontSendNotification);
            return false;
        }
        formulaInput.setText(juce::String::fromUTF8(glitch.expression.c_str()), juce::dontSendNotification);
        if (!compileAndApplyFormula()) return false;
        directEnabled.store(true, std::memory_order_release);
        directMode.setToggleState(true, juce::dontSendNotification);
        resetDirect.store(true, std::memory_order_release);
        playing.store(true, std::memory_order_release);
        risco::FormulaOrigin origin;
        origin.author = parent.formulaOrigin.author.empty()
            ? "Lúcio de Araújo / Risco"
            : std::string("Lúcio de Araújo / Risco; ") + parent.formulaOrigin.author;
        if (origin.author.size() > 128) origin.author = "Lúcio de Araújo / Risco";
        origin.source = "risco:glitch-math-v1";
        origin.license = parent.formulaOrigin.license == "AGPL-3.0-or-later"
            ? "AGPL-3.0-or-later" : "rights-unverified";
        origin.parentA = parent.id;
        origin.sourceId = "parent=" + std::to_string(parent.id) + ";kind=" + glitch.kind +
            ";a=" + std::to_string(glitch.parameterA) + ";b=" +
            std::to_string(glitch.parameterB) + ";int=" + std::to_string(intensity);
        const auto name = "Glitch " + glitch.kind + " " + std::to_string(intensity) + "%";
        if (!appendFormulaToBank(std::move(origin), name)) return false;
        status.setText(utf8("Glitch ") + juce::String::fromUTF8(glitch.kind.c_str()) +
            utf8(" aplicado a célula ") + juce::String(parent.id) + utf8(" · ") +
            juce::String(intensity) + utf8("% · derivado no STEM ") + juce::String(pendingStem + 1) +
            utf8(" · inserido e em pré-escuta."),
            juce::dontSendNotification);
        return true;
    }

    bool replayFormulaCell(const risco::Cell& cell) {
        if (!cell.bytebeatFormula) return false;
        int start = 0;
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& p = song.grid[static_cast<size_t>(i)];
            start = std::max(start, p.startBeat + placementBeats(p));
        }
        if (start > 4096 || song.placementCount >= int(song.grid.size())) return false;
        formulaInput.setText(juce::String::fromUTF8(cell.formulaExpression.c_str()),
                             juce::dontSendNotification);
        if (!compileAndApplyFormula()) return false;
        directEnabled.store(true, std::memory_order_release);
        directMode.setToggleState(true, juce::dontSendNotification);
        playing.store(true, std::memory_order_release);
        risco::Placement replayed{cell.id, start, risco::CellSpan::fourBars, 1};
        replayed.stem = pendingStem;
        const auto gridBefore = captureGridState();
        const bool placed = risco::placeCell(song, replayed);
        if (placed) {
            pushGridUndo(gridBefore);
            ensurePlacementVisible(song.placementCount - 1);
            repaint();
        }
        return placed;
    }

    juce::Rectangle<int> drawingArea() const { return {96, 364, logicalWidth - 192, 116}; }
    juce::Rectangle<int> gridArea() const { return {20, 550, logicalWidth - 40, 148}; }
    void paintDrawing(juce::Graphics& g) {
        const auto panel = juce::Rectangle<float>(80.0f, 332.0f, float(logicalWidth - 160), 160.0f);
        const auto canvas = drawingArea().toFloat();
        g.setColour(juce::Colour(0xff102b38));
        g.fillRoundedRectangle(panel, 8.0f);
        g.setColour(juce::Colour(0xff65d9e8).withAlpha(0.55f));
        g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.2f);
        g.setColour(juce::Colour(0xffcad2df));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(15.0f))));
        drawCanvasText(g, textEntryMode ? uiText("ENTRADA ÚNICA · TEXTO", "INPUT · TEXT", "ENTRÉE · TEXTE", "ENTRADA · TEXTO")
                                 : uiText("ENTRADA ÚNICA · DESENHO", "INPUT · DRAWING", "ENTRÉE · DESSIN", "ENTRADA · DIBUJO"),
                   96, 337, 420, 24, juce::Justification::left);
        g.setColour(juce::Colour(0xffa9b5c6));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(12.0f))));
        const auto entryHint = textEntryMode
            ? uiText("UTF-8 · acentos e pontuação · clique em Desenhar para riscar", "UTF-8 · accents and punctuation · choose Draw to switch modes", "UTF-8 · accents et ponctuation · choisir Dessiner", "UTF-8 · acentos y puntuación · elija Dibujar")
            : juce::String(inscription.count) + uiText(" / 12 traços · arraste desenha · Alt+arraste desloca · X silencia", " / 12 strokes · drag draws · Alt+drag moves · X silences", " / 12 traits · glisser dessine · Alt+glisser déplace · X silence", " / 12 trazos · arrastrar dibuja · Alt+arrastrar mueve · X silencia");
        drawCanvasText(g, entryHint,
                   600, 339, 568, 20, juce::Justification::right);
        g.setColour(juce::Colour(0xff0c1728));
        g.fillRoundedRectangle(canvas, 5.0f);
        for (int row = 1; row < 4; ++row) {
            const float y = canvas.getY() + canvas.getHeight() * float(row) / 4.0f;
            g.setColour(juce::Colour(0xff65d9e8).withAlpha(row == 2 ? 0.23f : 0.12f));
            for (int column = 1; column < 16; ++column) {
                const float x = canvas.getX() + canvas.getWidth() * float(column) / 16.0f;
                g.fillRect(x - 1.0f, y - 1.0f, 2.0f, 2.0f);
            }
        }
        if (!textEntryMode && inscription.count == 0) {
            g.setColour(juce::Colour(0xff81929e));
            g.setFont(canvasFont(juce::Font(juce::FontOptions(13.0f))));
            drawCanvasText(g, uiText("Comece um traço aqui — a leitura acompanha o gesto", "Start a stroke here · playback follows the gesture", "Commencez un trait ici · la lecture suit le geste", "Empiece un trazo aquí · la lectura sigue el gesto"),
                       canvas.toNearestInt(), juce::Justification::centred);
        }
        const double scanDuration = std::max(0.18, liveDuration.load(std::memory_order_relaxed));
        const double scanPhase = std::fmod(std::max(0.0, playheadSeconds.load(std::memory_order_relaxed)) / scanDuration, 1.0);
        const float scanShift = float(scanPhase * canvas.getWidth());
        g.saveState();
        g.reduceClipRegion(canvas.toNearestInt());
        for (int s = 0; s < inscription.count; ++s) {
            const auto& stroke = inscription.strokes[static_cast<size_t>(s)];
            if (stroke.count == 0) continue;
            float strokeShift = scanShift;
            if (drawing && s == inscription.count - 1) strokeShift = 0.0f;
            else if (s == releasedStrokeIndex) {
                const float release = std::clamp(float(juce::Time::getMillisecondCounter() - releasedStrokeMillis) / 250.0f,
                                                 0.0f, 1.0f);
                strokeShift -= releasedStrokeShift * (1.0f - release);
            }
            auto point = [&](int p, float shift) {
                const auto& v = stroke.points[static_cast<size_t>(p)];
                return juce::Point<float>(canvas.getX() + v.x * canvas.getWidth() - strokeShift + shift,
                                          canvas.getY() + v.y * canvas.getHeight());
            };
            const auto colour = juce::Colour(stroke.crossed ? crossedGraffitiColour
                : graffitiColours[static_cast<std::size_t>(s) % graffitiColours.size()]);
            const auto smoothedPressure = [&stroke](int index) {
                float sum = 0.0f;
                float weightSum = 0.0f;
                for (int offset = -1; offset <= 1; ++offset) {
                    const int at = std::clamp(index + offset, 0, stroke.count - 1);
                    const auto& sample = stroke.points[static_cast<size_t>(at)];
                    const float value = sample.pressure >= 0.0f && sample.pressure <= 1.0f
                        ? sample.pressure : 1.0f - sample.y;
                    const float weight = offset == 0 ? 2.0f : 1.0f;
                    sum += value * weight;
                    weightSum += weight;
                }
                return sum / weightSum;
            };
            for (const float wrap : {-canvas.getWidth(), 0.0f, canvas.getWidth()}) {
                juce::Path path;
                path.startNewSubPath(point(0, wrap));
                for (int p = 1; p < stroke.count; ++p) path.lineTo(point(p, wrap));
                g.setColour(colour.withAlpha(0.20f));
                g.strokePath(path, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
                g.setColour(colour);
                g.strokePath(path, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
                for (int p = 1; p < stroke.count; ++p) {
                    const float aPressure = smoothedPressure(p - 1);
                    const float bPressure = smoothedPressure(p);
                    const float thickness = 1.4f + 2.0f * std::clamp((aPressure + bPressure) * 0.5f, 0.0f, 1.0f);
                    const auto from = point(p - 1, wrap), to = point(p, wrap);
                    g.drawLine(from.x, from.y, to.x, to.y, thickness);
                }
            }
            const auto start = point(0, 0.0f);
            const auto end = point(stroke.count - 1, 0.0f);
            g.setColour(colour);
            g.fillRect(start.x - 3.0f, start.y - 3.0f, 6.0f, 6.0f);
            if (stroke.count > 1) g.fillEllipse(end.x - 3.0f, end.y - 3.0f, 6.0f, 6.0f);
            if (stroke.crossed) {
                const float x = end.x + 9.0f, y = end.y - 9.0f;
                g.drawLine(x - 4.0f, y - 4.0f, x + 4.0f, y + 4.0f, 2.0f);
                g.drawLine(x - 4.0f, y + 4.0f, x + 4.0f, y - 4.0f, 2.0f);
            }
        }
        g.setColour(juce::Colour(0xffffc65c));
        g.fillRect(canvas.getCentreX() - 1.5f, canvas.getY(), 3.0f, canvas.getHeight());
        g.restoreState();
    }
    void paintScope(juce::Graphics& g) {
        const auto panel = juce::Rectangle<float>(20.0f, 702.0f, float(logicalWidth - 40), 142.0f);
        const auto chart = juce::Rectangle<float>(32.0f, 738.0f, float(logicalWidth - 64), 84.0f);
        g.setColour(juce::Colour(0xff252a35));
        g.fillRoundedRectangle(panel, 8.0f);
        g.setColour(juce::Colour(0xffcad2df));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(15.0f))));
        const int selected = selectedCellIndex();
        const auto* archivedCell = selected >= 0 ? &song.bank[static_cast<size_t>(selected)] : nullptr;
        const bool showArchived = archivedCell != nullptr && archivedCell->waveformCount > 0;
        drawCanvasText(g, showArchived ? uiText("FORMA DA FUNÇÃO ARQUIVADA", "STORED FORMULA WAVEFORM", "ONDE DE LA FORMULE ENREGISTRÉE", "ONDA DE LA FÓRMULA GUARDADA") :
                   (directEnabled.load(std::memory_order_relaxed) ?
                        (compiledFormulaExpression.rfind("f:", 0) == 0 ? utf8("FLOATBEAT DIRETO / 8 kHz") : utf8("BYTEBEAT DIRETO / 8 BITS")) :
                        (song.bitDepth == 16 ? uiText("ONDA DE ÁUDIO / 16 BITS", "WAVEFORM / 16 BIT", "ONDE AUDIO / 16 BITS", "ONDA DE AUDIO / 16 BITS") :
                                               uiText("ONDA DE ÁUDIO / 8 BITS", "WAVEFORM / 8 BIT", "ONDE AUDIO / 8 BITS", "ONDA DE AUDIO / 8 BITS"))),
                   32, 707, 360, 22, juce::Justification::left);
        std::array<float, 256> samples{};
        const auto published = publishedScopeWriteIndex.load(std::memory_order_acquire);
        const auto valid = static_cast<size_t>(std::min<std::uint64_t>(published, scope.size()));
        const auto oldest = published - valid;
        float peak = 0.0f;
        for (size_t i = 0; i < samples.size(); ++i) {
            float sample = 0.0f;
            if (showArchived) {
                const auto sourceIndex = i * static_cast<size_t>(archivedCell->waveformCount) /
                                         samples.size();
                sample = float(archivedCell->waveform[sourceIndex]) / 32767.0f;
            } else if (valid > 0) {
                const auto offset = i * (valid - 1) / (samples.size() - 1);
                sample = scope[static_cast<size_t>((oldest + offset) % scope.size())]
                             .load(std::memory_order_relaxed);
            }
            samples[i] = sample;
            peak = std::max(peak, std::abs(sample));
        }
        const float zoom = peak > 0.001f ? std::clamp(0.88f / peak, 1.0f, 4.0f) : 1.0f;
        g.setFont(canvasFont(juce::Font(juce::FontOptions(10.0f))));
        drawCanvasText(g, uiText("ONDA ", "WAVE ", "ONDE ", "ONDA ") + juce::String(peak * 100.0f, 1)
            + "%  ZOOM x" + juce::String(zoom, 1) + (showArchived
                ? uiText("  ARQUIVO", "  STORED", "  ARCHIVE", "  ARCHIVO")
                : uiText("  2048 AMOSTRAS", "  2048 SAMPLES", "  2048 ÉCHANTILLONS", "  2048 MUESTRAS")),
            32, 826, logicalWidth - 64, 14,
            juce::Justification::right);
        g.setColour(juce::Colour(0xff171d24));
        g.fillRoundedRectangle(chart, 5.0f);
        for (int i = 1; i < 8; ++i) {
            const float x = chart.getX() + chart.getWidth() * float(i) / 8.0f;
            g.setColour(juce::Colour(i == 4 ? 0xff354b50 : 0xff29343d));
            g.drawVerticalLine(juce::roundToInt(x), chart.getY() + 2.0f, chart.getBottom() - 2.0f);
        }
        for (int i = 1; i < 4; ++i) {
            const float y = chart.getY() + chart.getHeight() * float(i) / 4.0f;
            g.setColour(juce::Colour(i == 2 ? 0xff52636b : 0xff29343d));
            g.drawHorizontalLine(juce::roundToInt(y), chart.getX() + 2.0f, chart.getRight() - 2.0f);
        }
        const float outputPeak = outputTruePeak.load(std::memory_order_acquire);
        const float reduction = outputReductionDb.load(std::memory_order_acquire);
        juce::Path wave;
        const float yMid = chart.getCentreY();
        const float halfHeight = chart.getHeight() * 0.47f;
        for (size_t i = 0; i < samples.size(); ++i) {
            const float x = chart.getX() + float(i) * chart.getWidth() / float(samples.size() - 1);
            const float y = yMid - std::clamp(samples[i] * zoom, -1.0f, 1.0f) * halfHeight;
            if (i == 0) wave.startNewSubPath(x, y);
            else wave.lineTo(x, y);
        }
        const auto waveColour = juce::Colour(showArchived
            ? graffitiColours[static_cast<std::size_t>(archivedCell->id) % graffitiColours.size()]
            : graffitiColours[1]);
        g.setColour(waveColour.withAlpha(0.30f));
        g.strokePath(wave, juce::PathStrokeType(7.0f));
        g.setColour(waveColour);
        g.strokePath(wave, juce::PathStrokeType(2.2f));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(12.0f))));
        {
            g.setColour(juce::Colour(0xff171d24).withAlpha(0.92f));
            g.fillRoundedRectangle(40.0f, 740.0f, 790.0f, 22.0f, 3.0f);
            g.setColour(juce::Colour(reduction > 12.0f ? 0xffff786a :
                                     reduction > 3.0f ? 0xffffc65c : 0xff61e2f0));
            const auto lufs = [](float value, bool ready) { return !ready ? juce::String("--") : value <= -70 ? juce::String("<=-70") : juce::String(value,1); };
            drawCanvasText(g, uiText("SAÍDA ", "OUTPUT ", "SORTIE ", "SALIDA ") + juce::String(20.0f * std::log10(std::max(outputPeak,1e-6f)), 1) +
                " dBTP | RMS " + juce::String(20.0f * std::log10(std::max(1e-6f, outputRms.load(std::memory_order_acquire))), 1) +
                " dBFS | LIMIT " + juce::String(reduction, 1) + " | LUFS M " + lufs(outputLufsM.load(),outputLufsMReady.load()) +
                " S " + lufs(outputLufsS.load(),outputLufsSReady.load()) + " I " + lufs(outputLufsI.load(),outputLufsIReady.load()),
                44, 742, 780, 18, juce::Justification::left);
        }
        g.setColour(juce::Colour(0xff171d24).withAlpha(0.92f));
        g.fillRoundedRectangle(836.0f, 740.0f, 412.0f, 22.0f, 3.0f);
        const float heldPeak = outputTruePeakHold.load(std::memory_order_acquire);
        const bool clipped = outputClipLatched.load(std::memory_order_acquire);
        g.setColour(juce::Colour(clipped ? 0xffff786a : 0xffa9b5c6));
        drawCanvasText(g, "HOLD " + juce::String(20.0f * std::log10(std::max(heldPeak, 1.0e-6f)), 1) +
            " | CORR " + juce::String(outputCorrelation.load(std::memory_order_acquire), 2) +
            " | MONO " + juce::String(20.0f * std::log10(std::max(outputMonoRms.load(std::memory_order_acquire), 1.0e-6f)), 1) +
            (clipped ? " | CLIP" : " | OK"), logicalWidth - 440, 742, 408, 18,
            juce::Justification::right);
    }
    void paintGrid(juce::Graphics& g) {
        const auto area = gridArea().toFloat();
        g.setColour(juce::Colour(0xff30271d));
        g.fillRoundedRectangle(area, 8.0f);
        g.setColour(juce::Colour(0xffffbf55).withAlpha(0.62f));
        g.drawRoundedRectangle(area.reduced(0.5f), 8.0f, 1.2f);
        g.setColour(juce::Colour(0xffcad2df));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(15.0f))));
        juce::String activity = song.placementCount > 0
            ? uiText("GRADE · mova, recorte ou toque as células", "GRID · move, trim or play cells", "GRILLE · déplacer, couper ou jouer les cellules", "CUADRÍCULA · mueva, recorte o toque las células")
            : song.cellCount > 0
                ? uiText("BANCO · selecione uma célula e insira na grade", "BANK · choose a cell and place it on the grid", "BANQUE · choisir une cellule et la placer dans la grille", "BANCO · elija una célula y colóquela en la cuadrícula")
                : uiText("GRADE VAZIA · use Faixa completa ou insira uma célula", "EMPTY GRID · use Full track or place a cell", "GRILLE VIDE · choisir Morceau complet ou placer une cellule", "CUADRÍCULA VACÍA · use Pieza completa o inserte una célula");
        if (!playing.load(std::memory_order_acquire) && selectedPlacement >= 0 &&
            selectedPlacement < song.placementCount) {
            const auto* selected = risco::findCell(song, song.grid[static_cast<size_t>(selectedPlacement)].cellId);
            if (selected != nullptr)
                activity = uiText("SELECIONADA · ", "SELECTED · ", "SÉLECTION · ", "SELECCIÓN · ")
                    + juce::String::fromUTF8(selected->name.c_str()) +
                    utf8(" · STEM ") + juce::String(song.grid[static_cast<size_t>(selectedPlacement)].stem + 1) +
                    uiText(" · Ouvir célula · bordas recortam · centro move · Retirar célula",
                           " · Play cell · trim at edges · move at center · Remove cell",
                           " · Écouter cellule · couper aux bords · déplacer au centre",
                           " · Oír célula · recorte en bordes · mueva desde el centro");
        }
        std::array<bool, 128> activePlacements{};
        if (playing.load(std::memory_order_acquire) && !seedTrackPreview) {
            if (const auto* playback = !previewingCell
                    ? activeSong.load(std::memory_order_acquire) : nullptr) {
                const double seconds = playheadSeconds.load(std::memory_order_relaxed);
                int activeCount = 0;
                juce::String activeNames;
                for (int i = 0; i < playback->count; ++i) {
                    const auto& item = playback->placements[static_cast<size_t>(i)];
                    if (seconds >= item.start && seconds < item.start + item.playLength) {
                        if (item.gridIndex >= 0 && item.gridIndex < int(activePlacements.size()))
                            activePlacements[static_cast<size_t>(item.gridIndex)] = true;
                        ++activeCount;
                        const auto* source = risco::findCell(song, item.cellId);
                        juce::String kind = item.bytebeatFormula ? utf8("FÓRMULA") : utf8("DESENHO");
                        if (source != nullptr && !item.bytebeatFormula) {
                            if (source->generatedPhrase) kind = utf8("GERAÇÃO");
                            else if (!source->inscription.ascii.empty() && source->inscription.count > 0)
                                kind = utf8("TEXTO + DESENHO");
                            else if (!source->inscription.ascii.empty()) kind = utf8("TEXTO");
                        }
                        if (activeCount <= 2) {
                            if (activeNames.isNotEmpty()) activeNames += utf8(" + ");
                            const auto lane = item.gridIndex >= 0 && item.gridIndex < song.placementCount
                                ? utf8("STEM ") + juce::String(song.grid[static_cast<size_t>(item.gridIndex)].stem + 1) + " · "
                                : juce::String();
                            activeNames += lane + kind + " · " +
                                (source != nullptr
                                    ? juce::String::fromUTF8(source->name.data(), int(source->name.size()))
                                    : utf8("célula"));
                        }
                    }
                }
                if (activeCount == 0) activity = utf8("TOCANDO · intervalo sem célula");
                else activity = utf8("TOCANDO · ") + activeNames +
                    (activeCount > 2 ? utf8(" + ") + juce::String(activeCount - 2) + utf8(" outras") : juce::String());
            } else {
                const auto source = previewingCell ? utf8("CÉLULA") :
                    directEnabled.load(std::memory_order_relaxed) ||
                    formulaEnabled.load(std::memory_order_relaxed) ? utf8("FÓRMULA") :
                    !inscription.ascii.empty() ? utf8("TEXTO") : utf8("DESENHO");
                activity = (previewingCell ? utf8("PRÉVIA · ") : utf8("AO VIVO · ")) +
                    source + utf8(" · fora da grade");
            }
        }
        g.setColour(juce::Colour(0xffcad2df));
        drawCanvasText(g, activity, 32, 555, 570, 25, juce::Justification::left, true);
        const bool showingSongClock = activeSong.load(std::memory_order_acquire) != nullptr &&
                                      !previewingCell && !seedTrackPreview &&
                                      playing.load(std::memory_order_acquire);
        if (showingSongClock) {
            const double seconds = std::max(0.0, playheadSeconds.load(std::memory_order_relaxed));
            const double beatPosition = seconds * song.bpm / 60.0;
            const int bar = int(beatPosition / song.beatsPerBar) + 1;
            const int beat = int(std::fmod(beatPosition, song.beatsPerBar)) + 1;
            const int minutes = int(seconds / 60.0);
            const int remainder = int(seconds) % 60;
            const auto clock = utf8("COMPASSO ") + juce::String(bar) +
                utf8(" · TEMPO ") + juce::String(beat) + " · " +
                juce::String::formatted("%02d:%02d", minutes, remainder);
            g.setColour(juce::Colour(0xffffd078));
            g.setFont(canvasFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold))));
            drawCanvasText(g, clock, logicalWidth - 390, 555, 250, 25,
                       juce::Justification::right, true);
        }
        g.setColour(juce::Colour(0xff91a8b6));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(10.0f))));
        g.setColour(juce::Colour(0xff9fb2c2));
        g.setFont(canvasFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold))));
        drawCanvasText(g, utf8("CANAIS 1–5"), 25, 572, 84, 12, juce::Justification::centredLeft);
        drawCanvasText(g, "VOL", 1040, 572, 74, 12, juce::Justification::centredLeft);
        drawCanvasText(g, "M", 1118, 572, 26, 12, juce::Justification::centred);
        drawCanvasText(g, "S", 1148, 572, 26, 12, juce::Justification::centred);
        drawCanvasText(g, "PAN", 1180, 572, 76, 12, juce::Justification::centredLeft);
        const int totalBeats = gridTotalBeats();
        const double viewStart = gridViewStart();
        const int viewBeats = gridViewSpan();
        const float left = gridTimelineLeft();
        const float width = gridTimelineWidth();
        const float scale = width / float(viewBeats);
        const int barStride = std::max(1, (viewBeats + song.beatsPerBar * 31 - 1) /
                                      (song.beatsPerBar * 64));
        g.saveState();
        g.reduceClipRegion(area.toNearestInt());
        for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
            const float rowY = 586.0f + float(stem) * 22.0f;
            g.setColour(juce::Colour(stem == 1 ? 0xff243444 : 0xff1e2d3b).withAlpha(0.72f));
            g.fillRoundedRectangle(24.0f, rowY, float(logicalWidth - 48), 20.0f, 3.0f);
            if (draggedPlacement >= 0 && placementDragMoved && placementDragStem == stem) {
                g.setColour(juce::Colour(0xff56d6ad).withAlpha(0.16f));
                g.fillRoundedRectangle(24.0f, rowY, float(logicalWidth - 48), 20.0f, 3.0f);
            }
            g.setColour(juce::Colour(0xff9fb2c2));
            g.setFont(canvasFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold))));
            drawCanvasText(g, "S" + juce::String(stem + 1), 25, int(rowY), 20, 20,
                       juce::Justification::centredLeft, false);
        }
        g.setColour(juce::Colour(0xff637384));
        g.drawVerticalLine(1032, 576.0f, 697.0f);
        g.reduceClipRegion(juce::Rectangle<int>(juce::roundToInt(left), 584,
                                                juce::roundToInt(width), 113));
        if (loopRangeEndBeat > loopRangeStartBeat) {
            const float rangeX = left + (float(loopRangeStartBeat) - float(viewStart)) * scale;
            const float rangeW = (float(loopRangeEndBeat) - float(loopRangeStartBeat)) * scale;
            g.setColour(juce::Colour(0xff56d6ad).withAlpha(
                loopRangeEnabled.load(std::memory_order_acquire) ? 0.18f : 0.08f));
            g.fillRect(rangeX, 584.0f, rangeW, 112.0f);
            g.setColour(juce::Colour(0xff56d6ad).withAlpha(0.88f));
            g.drawVerticalLine(juce::roundToInt(rangeX), 584.0f, 696.0f);
            g.drawVerticalLine(juce::roundToInt(rangeX + rangeW), 584.0f, 696.0f);
        }
        g.setColour(juce::Colour(0xff465063));
        const int lineWidth = song.beatsPerBar * barStride;
        const int firstLine = int(std::floor(viewStart / lineWidth)) * lineWidth;
        const int lastLine = std::min(totalBeats, int(std::ceil(viewStart + viewBeats)));
        for (int beat = firstLine; beat <= lastLine;
             beat += song.beatsPerBar * barStride) {
            const float x = left + (float(beat) - float(viewStart)) * scale;
            g.drawVerticalLine(juce::roundToInt(x), 585.0f, 696.0f);
        }
        for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
            const int selected = song.stemMeter[static_cast<size_t>(stem)];
            if (selected == 0) continue;
            const int meterBeats = selected == 8 ? 3 : selected;
            const int stemStride = std::max(1, (viewBeats + meterBeats * 31 - 1) / (meterBeats * 64));
            const int step = meterBeats * stemStride;
            const int first = int(std::floor(viewStart / step)) * step;
            g.setColour(juce::Colour(0xffffc65c).withAlpha(0.34f));
            for (int beat = first; beat <= lastLine; beat += step) {
                const float x = left + (float(beat) - float(viewStart)) * scale;
                const float y = 586.0f + float(stem) * 22.0f;
                g.drawVerticalLine(juce::roundToInt(x), y, y + 28.0f);
            }
        }
        for (int pass = 0; pass < song.placementCount; ++pass) {
            const int i = selectedPlacement >= 0 && selectedPlacement < song.placementCount
                ? (pass == song.placementCount - 1 ? selectedPlacement
                    : pass >= selectedPlacement ? pass + 1 : pass)
                : pass;
            const auto& p = song.grid[static_cast<size_t>(i)];
            const auto* cell = risco::findCell(song, p.cellId);
            if (cell == nullptr) continue;
            const int visibleStart = i == draggedPlacement && placementDragMoved
                ? placementDragBeat : p.startBeat;
            const int visibleLength = i == draggedPlacement && placementDragMoved
                ? placementDragLength : placementBeats(p);
            const int visibleSource = i == draggedPlacement && placementDragMoved
                ? placementDragSource : p.sourceStartBeat;
            const int visibleStem = i == draggedPlacement && placementDragMoved
                ? placementDragStem : p.stem;
            const float x = left + (float(visibleStart) - float(viewStart)) * scale;
            const float w = std::max(2.0f, visibleLength * scale - 2.0f);
            const float y = 587.0f + float(visibleStem) * 22.0f;
            const bool isActive = activePlacements[static_cast<size_t>(i)];
            if (isActive) {
                g.setColour(juce::Colour(0xffffc65c).withAlpha(0.25f));
                g.fillRoundedRectangle(x - 3.0f, y - 2.0f, w + 6.0f, 22.0f, 5.0f);
            }
            auto blockColour = juce::Colour(graffitiColours[static_cast<std::size_t>(p.cellId) % graffitiColours.size()]);
            if (i == draggedPlacement && placementDragMoved) blockColour = blockColour.brighter(0.25f);
            g.setColour(blockColour);
            g.fillRoundedRectangle(x, y, w, 24.0f, 4.0f);
            if (w > 24.0f && cell->waveformCount > 1) {
                const int points = std::clamp(juce::roundToInt(w - 8.0f), 2,
                                              std::min(cell->waveformCount, 128));
                juce::Path miniWave;
                for (int sample = 0; sample < points; ++sample) {
                    const int sourceIndex = sample * (cell->waveformCount - 1) /
                                            std::max(1, points - 1);
                    const float value = float(cell->waveform[static_cast<size_t>(sourceIndex)]) / 32768.0f;
                    const float px = x + 4.0f + float(sample) * (w - 8.0f) / float(points - 1);
                    const float py = y + 12.0f - value * 7.0f;
                    if (sample == 0) miniWave.startNewSubPath(px, py);
                    else miniWave.lineTo(px, py);
                }
                g.setColour(juce::Colour(0xfff4fcff).withAlpha(i == selectedPlacement ? 0.76f : 0.48f));
                g.strokePath(miniWave, juce::PathStrokeType(1.0f));
            }
            if (i == selectedPlacement) {
                g.setColour(juce::Colour(0xfff4fcff).withAlpha(0.90f));
                g.drawRoundedRectangle(x + 0.5f, y + 0.5f, w - 1.0f, 23.0f, 4.0f, 1.5f);
            }
            if (w >= 18.0f) {
                g.setColour(juce::Colour(0xff16384e).withAlpha(i == selectedPlacement ? 0.95f : 0.65f));
                g.fillRoundedRectangle(x + 3.0f, y + 5.0f, 3.0f, 14.0f, 1.0f);
                g.fillRoundedRectangle(x + w - 6.0f, y + 5.0f, 3.0f, 14.0f, 1.0f);
            }
            if (w > 45.0f) {
                g.setColour(juce::Colour(0xff15171d));
                g.setFont(canvasFont(juce::Font(juce::FontOptions(13.0f))));
                auto label = juce::String::fromUTF8(cell->name.data(), int(cell->name.size()));
                if (w > 100.0f && (visibleSource > 0 || p.lengthBeats > 0 ||
                                    (i == draggedPlacement && placementDragMoved)))
                    label += utf8(" · ") + juce::String(visibleSource + 1) + utf8("–") +
                             juce::String(visibleSource + visibleLength);
                drawCanvasText(g, label,
                           juce::roundToInt(x + 5.0f), juce::roundToInt(y),
                           juce::roundToInt(w - 10.0f), 24, juce::Justification::centredLeft, true);
            }
        }
        if (!previewingCell && !seedTrackPreview && playing.load(std::memory_order_acquire) &&
            activeSong.load(std::memory_order_acquire) != nullptr) {
            const float beat = float(playheadSeconds.load(std::memory_order_relaxed) * song.bpm / 60.0);
            const float x = left + (beat - float(viewStart)) * scale;
            if (x >= left && x <= left + width) {
                g.setColour(juce::Colour(0xffffc65c));
            g.fillRect(x - 1.5f, 584.0f, 3.0f, 112.0f);
                juce::Path marker;
                marker.startNewSubPath(x - 5.0f, 583.0f);
                marker.lineTo(x + 5.0f, 583.0f);
                marker.lineTo(x, 589.0f);
                marker.closeSubPath();
                g.fillPath(marker);
            }
        }
        g.restoreState();
    }
    enum class GridDragMode { none, move, trimLeft, trimRight };
    int placementBeats(const risco::Placement& placement) const {
        return int(std::lround(risco::placementLengthSeconds(song, placement) * song.bpm / 60.0));
    }
    int gridTotalBeats() const {
        int total = song.beatsPerBar * 8;
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& p = song.grid[static_cast<size_t>(i)];
            total = std::max(total, p.startBeat + placementBeats(p));
        }
        return total;
    }
    int gridViewSpan() const {
        const int total = gridTotalBeats();
        return std::clamp(gridZoomBeats > 0 ? gridZoomBeats : song.beatsPerBar * 8,
                          std::min(4, total), total);
    }
    void zoomGrid(float factor) {
        const int oldSpan = gridViewSpan();
        const int total = gridTotalBeats();
        const double anchor = gridViewStart() + double(oldSpan) * 0.5;
        gridZoomBeats = std::clamp(int(std::round(float(oldSpan) * factor)),
                                   std::min(4, total), total);
        gridScrollBeat = anchor - double(gridZoomBeats) * 0.5;
        gridScrollBeat = gridViewStart();
        repaint();
    }
    double gridViewStart() const {
        return std::clamp(gridScrollBeat, 0.0,
                          double(std::max(0, gridTotalBeats() - gridViewSpan())));
    }
    void ensurePlacementVisible(int index) {
        if (index < 0 || index >= song.placementCount) return;
        const auto& placement = song.grid[static_cast<size_t>(index)];
        const double start = gridViewStart();
        const int end = placement.startBeat + placementBeats(placement);
        if (placement.startBeat < start) gridScrollBeat = placement.startBeat;
        else if (end > start + gridViewSpan()) gridScrollBeat = end - gridViewSpan();
        gridScrollBeat = gridViewStart();
    }
    void publishLoopRange() {
        const double secondsPerBeat = 60.0 / std::clamp(song.bpm, 40.0, 240.0);
        loopRangeStartSeconds.store(loopRangeStartBeat * secondsPerBeat, std::memory_order_release);
        loopRangeEndSeconds.store(loopRangeEndBeat * secondsPerBeat, std::memory_order_release);
    }
    int beatAtGridX(int x) const {
        const float scale = gridTimelineWidth() / float(gridViewSpan());
        return std::clamp(int(std::floor(gridViewStart() +
                          (float(x) - gridTimelineLeft()) / scale)), 0, 4096);
    }
    float gridTimelineLeft() const noexcept { return 116.0f; }
    float gridTimelineRight() const noexcept { return 1028.0f; } // 12 before mixer controls.
    float gridTimelineWidth() const noexcept { return gridTimelineRight() - gridTimelineLeft(); }
    int gridStemAtY(int y) const noexcept {
        return std::clamp((y - 586 + 11) / 22, 0, risco::Song::stemCount - 1);
    }
    int placementAtGridPoint(juce::Point<int> point, bool skipSelected = false) const {
        if (point.y < 585 || point.y > 696 || float(point.x) > gridTimelineRight()) return -1;
        const float scale = gridTimelineWidth() / float(gridViewSpan());
        const auto contains = [this, &point, scale](int index) {
            const auto& p = song.grid[static_cast<size_t>(index)];
            const float x = gridTimelineLeft() + (p.startBeat - gridViewStart()) * scale;
            const float w = std::max(2.0f, placementBeats(p) * scale - 2.0f);
            const float y = 587.0f + float(p.stem) * 22.0f;
            return float(point.x) >= x && float(point.x) <= x + w &&
                   float(point.y) >= y && float(point.y) <= y + 18.0f;
        };
        if (!skipSelected && selectedPlacement >= 0 && selectedPlacement < song.placementCount &&
            contains(selectedPlacement)) return selectedPlacement;
        for (int i = song.placementCount - 1; i >= 0; --i) {
            if (i != selectedPlacement && contains(i)) return i;
        }
        return -1;
    }
    GridDragMode gridDragModeAtPoint(int index, juce::Point<int> point) const {
        if (index < 0 || index >= song.placementCount) return GridDragMode::none;
        const auto& placement = song.grid[static_cast<size_t>(index)];
        const float scale = gridTimelineWidth() / float(gridViewSpan());
        const float x = gridTimelineLeft() + (placement.startBeat - gridViewStart()) * scale;
        const float width = std::max(2.0f, placementBeats(placement) * scale - 2.0f);
        const float grip = std::min(9.0f, std::max(5.0f, width * 0.3f));
        if (float(point.x) >= x + width - grip) return GridDragMode::trimRight;
        if (width >= 18.0f && float(point.x) <= x + grip) return GridDragMode::trimLeft;
        return GridDragMode::move;
    }
    void selectPlacement(int index) {
        if (index < 0 || index >= song.placementCount) return;
        selectedPlacement = index;
        const auto& placement = song.grid[static_cast<size_t>(index)];
        pendingStem = placement.stem;
        stemSelect.setSelectedId(pendingStem + 1, juce::dontSendNotification);
        for (int cell = 0; cell < song.cellCount; ++cell) {
            if (song.bank[static_cast<size_t>(cell)].id == placement.cellId) {
                bankSelect.setSelectedId(cell + 1, juce::sendNotification);
                break;
            }
        }
        startBeat.setText(juce::String(placement.startBeat));
    }
    void beginPlacementDrag(int index, juce::Point<int> point) {
        selectPlacement(index);
        placementDragBefore = captureGridState();
        grabKeyboardFocus();
        draggedPlacement = index;
        const auto& placement = song.grid[static_cast<size_t>(index)];
        gridDragMode = gridDragModeAtPoint(index, point);
        placementDragOriginalBeat = placement.startBeat;
        placementDragBeat = placementDragOriginalBeat;
        placementDragOriginalStem = placement.stem;
        placementDragStem = placementDragOriginalStem;
        placementDragOriginalLength = placementBeats(placement);
        placementDragLength = placementDragOriginalLength;
        placementDragOriginalSource = placement.sourceStartBeat;
        placementDragSource = placementDragOriginalSource;
        placementDragGrabOffset = beatAtGridX(point.x) - placementDragOriginalBeat;
        placementDragMoved = false;
        status.setText(gridDragMode == GridDragMode::move
                           ? utf8("Célula selecionada · arraste o centro; verticalmente muda de stem.")
                           : utf8("Arraste a borda: esquerda muda o início do trecho; direita muda a duração."),
                       juce::dontSendNotification);
    }
    void updatePlacementDrag(juce::Point<int> point) {
        if (draggedPlacement < 0 || draggedPlacement >= song.placementCount) return;
        if (gridDragMode == GridDragMode::move) {
            placementDragBeat = std::clamp(beatAtGridX(point.x) - placementDragGrabOffset,
                                           0, std::max(0, 4096 - placementDragOriginalLength));
            placementDragStem = gridStemAtY(point.y);
        } else if (gridDragMode == GridDragMode::trimRight) {
            const int end = std::clamp(beatAtGridX(point.x) + 1,
                                       placementDragOriginalBeat + 1, 4096);
            placementDragLength = end - placementDragOriginalBeat;
        } else if (gridDragMode == GridDragMode::trimLeft) {
            const int minStart = std::max(0, placementDragOriginalBeat - placementDragOriginalSource);
            const int maxStart = placementDragOriginalBeat +
                std::min(placementDragOriginalLength - 1, 4096 - placementDragOriginalSource);
            placementDragBeat = std::clamp(beatAtGridX(point.x), minStart, maxStart);
            const int delta = placementDragBeat - placementDragOriginalBeat;
            placementDragSource = placementDragOriginalSource + delta;
            placementDragLength = placementDragOriginalLength - delta;
        }
        placementDragMoved = placementDragBeat != placementDragOriginalBeat ||
                             placementDragLength != placementDragOriginalLength ||
                             placementDragStem != placementDragOriginalStem;
        repaint();
    }
    void finishPlacementDrag() {
        const int index = draggedPlacement;
        draggedPlacement = -1;
        if (placementDragMoved && index >= 0 && index < song.placementCount) {
            auto& placement = song.grid[static_cast<size_t>(index)];
            placement.startBeat = placementDragBeat;
            if (gridDragMode == GridDragMode::move) placement.stem = placementDragStem;
            if (gridDragMode == GridDragMode::trimLeft || gridDragMode == GridDragMode::trimRight) {
                placement.sourceStartBeat = placementDragSource;
                placement.lengthBeats = placementDragLength;
            }
            pushGridUndo(placementDragBefore);
            startBeat.setText(juce::String(placementDragBeat));
            refreshPlaybackAfterGridEdit();
            status.setText(gridDragMode == GridDragMode::move
                               ? utf8("Célula movida para o tempo ") + juce::String(placementDragBeat + 1) +
                                 utf8(" · STEM ") + juce::String(placementDragStem + 1) + "."
                               : utf8("Trecho: começa no tempo ") + juce::String(placementDragBeat + 1) +
                                 utf8(", usa fonte desde ") + juce::String(placementDragSource + 1) +
                                 utf8(" e dura ") + juce::String(placementDragLength) + utf8(" tempos."),
                           juce::dontSendNotification);
        } else {
            status.setText(utf8("Célula selecionada. Arraste bordas para recortar ou centro para mover."),
                           juce::dontSendNotification);
        }
        placementDragMoved = false;
        gridDragMode = GridDragMode::none;
        repaint();
    }
    void handleGridClick(juce::Point<int> point) {
        if (point.y < 585 || point.y > 696) return;
        const int beat = beatAtGridX(point.x);
        startBeat.setText(juce::String(beat));
        if (autoRouteButton.getToggleState()) {
            placeSelectedCell();
            return;
        }
        pendingStem = gridStemAtY(point.y);
        stemSelect.setSelectedId(pendingStem + 1, juce::dontSendNotification);
        placeSelectedCell();
    }
    int selectedCellIndex() const {
        const int index = bankSelect.getSelectedId() - 1;
        return index >= 0 && index < song.cellCount ? index : -1;
    }
    bool syncInscriptionText() {
        const auto text = ascii.getText().toStdString();
        inscription.ascii = text;
        if (!risco::validInscription(activeInput())) {
            status.setText(utf8("Texto inválido ou traço incompleto; confira a célula."),
                           juce::dontSendNotification);
            return false;
        }
        return true;
    }
    void startNextLibraryRead() {
        if (libraryPendingFiles.empty()) { openLibrarySearch(); return; }
        libraryActiveFile = libraryPendingFiles.front();
        libraryPendingFiles.erase(libraryPendingFiles.begin());
        libraryResult = std::make_shared<ProjectLibraryResult>();
        libraryPool.addJob(new ProjectLibraryJob(libraryActiveFile,libraryResult),true);
        status.setText(utf8("Indexando projeto ") + libraryActiveFile.getFileName() + utf8(" · música atual preservada."),
            juce::dontSendNotification);
    }
    void chooseLibraryProject() {
        if (libraryResult || !libraryPendingFiles.empty()) {
            status.setText(utf8("Uma leitura de projeto já está em andamento."), juce::dontSendNotification);
            return;
        }
        fileChooser = std::make_unique<juce::FileChooser>(utf8("Escolher até 8 projetos para o índice da sessão"),
            currentFile.existsAsFile() ? currentFile.getParentDirectory() : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.risco");
        juce::Component::SafePointer<MainComponent> safeThis(this);
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems,
            [safeThis](const juce::FileChooser& chooser) {
                if (!safeThis) return;
                const auto files = chooser.getResults();
                for (const auto& file : files) {
                    if (!file.existsAsFile()) continue;
                    const auto path = file.getFullPathName().toStdString();
                    const bool indexed = std::any_of(safeThis->libraryIndex.begin(),safeThis->libraryIndex.end(),
                        [&](const ProjectLibraryEntry& entry) { return entry.canonicalPath == path; });
                    const bool queued = std::any_of(safeThis->libraryPendingFiles.begin(),safeThis->libraryPendingFiles.end(),
                        [&](const juce::File& queuedFile) { return queuedFile == file; });
                    if (indexed || queued) continue;
                    if (safeThis->libraryIndex.size() + safeThis->libraryPendingFiles.size() >= 8) break;
                    const auto sourcePath = juce::String::fromUTF8(path.c_str());
                    const bool savedPath = safeThis->librarySourcePaths.contains(sourcePath,true);
                    if (!savedPath && safeThis->librarySourcePaths.size() >= 8) break;
                    if (!savedPath) safeThis->librarySourcePaths.add(sourcePath);
                    safeThis->libraryPendingFiles.push_back(file);
                }
                if (safeThis->libraryPendingFiles.empty()) {
                    safeThis->status.setText(utf8("Nenhum projeto novo escolhido; catálogo e índice foram preservados."),juce::dontSendNotification);
                    return;
                }
                safeThis->libraryCatalogSaveFailed = !safeThis->saveLibraryCatalog();
                safeThis->startNextLibraryRead();
            });
    }
    void createLibraryGroup() {
        if (libraryResult || !libraryPendingFiles.empty()) {
            status.setText(utf8("Aguarde a leitura atual antes de organizar o catálogo."),juce::dontSendNotification);
            return;
        }
        if (libraryGroups.size() >= 16) {
            status.setText(utf8("Limite de 16 agrupamentos; exclua um existente para criar outro."),juce::dontSendNotification);
            return;
        }
        fileChooser = std::make_unique<juce::FileChooser>(utf8("Escolher fontes para o agrupamento · até 8 projetos .risco"),
            currentFile.existsAsFile() ? currentFile.getParentDirectory() : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),"*.risco");
        juce::Component::SafePointer<MainComponent> safeThis(this);
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems,
            [safeThis](const juce::FileChooser& chooser) {
                if (!safeThis) return;
                juce::StringArray selectedPaths;
                for (const auto& file : chooser.getResults()) {
                    if (!file.existsAsFile() || !file.getFileExtension().equalsIgnoreCase(".risco")) continue;
                    const auto path = file.getFullPathName();
                    if (!selectedPaths.contains(path,true)) selectedPaths.add(path);
                    if (selectedPaths.size() == 8) break;
                }
                if (selectedPaths.isEmpty()) {
                    safeThis->status.setText(utf8("Nenhuma fonte .risco foi escolhida."),juce::dontSendNotification);
                    return;
                }
                safeThis->libraryGroupNameDialog = std::make_unique<juce::AlertWindow>(
                    utf8("Nomear agrupamento autoral"),
                    utf8("Até 64 caracteres. As fontes são referências; os projetos não são alterados."),
                    juce::MessageBoxIconType::NoIcon);
                safeThis->libraryGroupNameDialog->addTextEditor("name","",utf8("Nome do agrupamento:"));
                safeThis->libraryGroupNameDialog->addButton(safeThis->uiText("Criar", "Create", "Créer", "Crear"),1,juce::KeyPress(juce::KeyPress::returnKey));
                safeThis->libraryGroupNameDialog->addButton(safeThis->uiText("Cancelar", "Cancel", "Annuler", "Cancelar"),0,juce::KeyPress(juce::KeyPress::escapeKey));
                safeThis->libraryGroupNameDialog->enterModalState(true,juce::ModalCallbackFunction::create(
                    [safeThis,selectedPaths](int choice) {
                        if (!safeThis || !safeThis->libraryGroupNameDialog) return;
                        const auto name = safeThis->libraryGroupNameDialog->getTextEditorContents("name").trim();
                        safeThis->libraryGroupNameDialog.reset();
                        if (choice == 1) safeThis->saveLibraryGroup(name,selectedPaths);
                    }),false);
            });
    }
    void saveLibraryGroup(const juce::String& name, const juce::StringArray& paths) {
        if (!validProjectLibraryGroupName(name)) {
            status.setText(utf8("Nome inválido; use de 1 a 64 caracteres sem quebras de linha."),juce::dontSendNotification);
            return;
        }
        if (libraryGroups.size() >= 16 || std::any_of(libraryGroups.begin(),libraryGroups.end(),[&](const ProjectLibraryGroup& group) {
                return group.name.compareIgnoreCase(name) == 0;
            })) {
            status.setText(utf8("Nome repetido ou limite de 16 agrupamentos atingido."),juce::dontSendNotification);
            return;
        }
        auto nextSources = librarySourcePaths;
        for (const auto& path : paths)
            if (!nextSources.contains(path,true)) nextSources.add(path);
        if (nextSources.size() > 8) {
            status.setText(utf8("Este agrupamento excederia o limite total de oito fontes do catálogo."),juce::dontSendNotification);
            return;
        }
        auto nextGroups = libraryGroups;
        nextGroups.push_back({name,paths});
        const auto previousSources = librarySourcePaths;
        const auto previousGroups = libraryGroups;
        librarySourcePaths = std::move(nextSources);
        libraryGroups = std::move(nextGroups);
        if (!saveLibraryCatalog()) {
            librarySourcePaths = previousSources;
            libraryGroups = previousGroups;
            status.setText(utf8("Não foi possível salvar o agrupamento no catálogo local."),juce::dontSendNotification);
            return;
        }
        status.setText(utf8("Agrupamento autoral salvo. Reindexe o catálogo para buscar suas fontes."),juce::dontSendNotification);
        refreshBank(bankSelectionBeforeMenu,false);
    }
    void reindexSavedLibrary() {
        if (libraryResult || !libraryPendingFiles.empty()) {
            status.setText(utf8("Uma leitura de projeto já está em andamento."),juce::dontSendNotification);
            return;
        }
        libraryIndex.clear(); libraryCandidateSong.reset(); libraryCandidateCell = -1;
        libraryCandidateDescription.clear(); libraryReadErrors.clear();
        for (const auto& path : librarySourcePaths) {
            juce::File file(path);
            if (file.existsAsFile()) libraryPendingFiles.push_back(file);
            else libraryReadErrors.push_back(file.getFileName() + utf8(": caminho indisponível"));
        }
        if (libraryPendingFiles.empty()) {
            status.setText(librarySourcePaths.isEmpty()
                ? utf8("Catálogo persistente vazio; nenhum projeto foi lido.")
                : utf8("Nenhum caminho do catálogo está disponível; fontes preservadas."),juce::dontSendNotification);
            libraryReadErrors.clear();
            return;
        }
        startNextLibraryRead();
    }
    void clearSavedLibrary() {
        librarySourcePaths.clear();
        libraryGroups.clear();
        if (saveLibraryCatalog())
            status.setText(utf8("Catálogo persistente limpo; nenhum projeto-fonte foi alterado."),juce::dontSendNotification);
        else status.setText(utf8("Não foi possível limpar o catálogo persistente local."),juce::dontSendNotification);
    }
    void showLibraryMatches(juce::String query, juce::StringArray allowedSources = {}, juce::String groupName = {}) {
        const auto* sourceFilter = allowedSources.isEmpty() ? nullptr : &allowedSources;
        auto matches = std::make_shared<std::vector<ProjectLibraryMatch>>(
            searchProjectLibraries(libraryIndex,query,128,sourceFilter));
        if (matches->empty()) {
            status.setText(groupName.isEmpty() ? utf8("Nenhuma célula encontrada no índice da sessão.")
                : utf8("Nenhuma célula do agrupamento está no índice carregado."),juce::dontSendNotification);
            return;
        }
        juce::PopupMenu menu;
        menu.addSectionHeader(utf8("Escolher candidato · até 128 resultados"));
        for (size_t i = 0; i < matches->size(); ++i) {
            const auto& match = (*matches)[i];
            const auto& cell = match.song->bank[static_cast<size_t>(match.cellIndex)];
            const auto name = juce::String::fromUTF8(cell.name.c_str());
            menu.addItem(int(i)+1,juce::String::fromUTF8(match.displayName.c_str()) + utf8(" · ") +
                (name.isEmpty() ? utf8("Célula") : name) + " #" + juce::String(cell.id));
        }
        juce::Component::SafePointer<MainComponent> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&bankSelect),
            [safeThis,matches](int choice) {
                if (!safeThis || choice < 1 || choice > int(matches->size())) return;
                const auto& match = (*matches)[static_cast<size_t>(choice-1)];
                safeThis->libraryCandidateSong = match.song;
                safeThis->libraryCandidateCell = match.cellIndex;
                safeThis->libraryCandidateDescription = match.description;
                safeThis->refreshBank(safeThis->bankSelectionBeforeMenu,false);
                juce::PopupMenu actions;
                actions.addSectionHeader(utf8("Candidato externo · ainda não importado"));
                actions.addItem(1,utf8("Ouvir antes de importar"));
                actions.addItem(2,utf8("Importar para o banco"));
                actions.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&safeThis->bankSelect),
                    [safeThis](int action) {
                        if (!safeThis) return;
                        if (action == 1) safeThis->previewLibraryCandidate();
                        else if (action == 2) safeThis->importLibraryCandidate();
                    });
            });
    }
    void openLibrarySearch(ProjectLibraryGroup group = {}) {
        if (libraryIndex.empty()) {
            status.setText(utf8("O índice externo da sessão está vazio."),juce::dontSendNotification);
            return;
        }
        const auto title = group.name.isEmpty() ? utf8("Buscar no índice externo da sessão")
            : utf8("Buscar no agrupamento · ") + group.name;
        const auto count = group.sources.isEmpty() ? int(libraryIndex.size()) : int(std::count_if(
            libraryIndex.begin(),libraryIndex.end(),[&](const ProjectLibraryEntry& entry) {
                return group.sources.contains(juce::String::fromUTF8(entry.canonicalPath.c_str()),true);
            }));
        if (count == 0) {
            status.setText(utf8("Reindexe as fontes escolhidas antes de buscar neste agrupamento."),juce::dontSendNotification);
            return;
        }
        libraryDialog = std::make_unique<juce::AlertWindow>(title,
            juce::String(count) + utf8(" projeto(s) carregado(s). Nome, fórmula, autor, origem ou ID; até 128 resultados."),
            juce::MessageBoxIconType::NoIcon);
        libraryDialog->addTextEditor("query","",utf8("Busca:"));
        libraryDialog->addButton(uiText("Buscar", "Search", "Rechercher", "Buscar"),1,juce::KeyPress(juce::KeyPress::returnKey));
        libraryDialog->addButton(uiText("Cancelar", "Cancel", "Annuler", "Cancelar"),0,juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        const auto allowedSources = group.sources;
        const auto groupName = group.name;
        libraryDialog->enterModalState(true,juce::ModalCallbackFunction::create([safeThis,allowedSources,groupName](int choice) {
            if (!safeThis || !safeThis->libraryDialog) return;
            const auto query = safeThis->libraryDialog->getTextEditorContents("query").substring(0,128);
            safeThis->libraryDialog.reset();
            if (choice == 1) safeThis->showLibraryMatches(query,allowedSources,groupName);
        }),false);
    }
    void openLibraryGroupSearch() {
        if (libraryGroups.empty()) {
            status.setText(utf8("Nenhum agrupamento autoral no catálogo."),juce::dontSendNotification);
            return;
        }
        juce::PopupMenu menu;
        menu.addSectionHeader(utf8("Agrupamentos autorais"));
        for (size_t i = 0; i < libraryGroups.size(); ++i) {
            juce::PopupMenu actions;
            actions.addItem(int(i * 2) + 1,utf8("Buscar neste grupo"));
            actions.addItem(int(i * 2) + 2,utf8("Excluir agrupamento"));
            menu.addSubMenu(libraryGroups[i].name,actions);
        }
        const auto groups = libraryGroups;
        juce::Component::SafePointer<MainComponent> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&bankSelect),
            [safeThis,groups](int choice) {
                if (!safeThis || choice < 1 || choice > int(groups.size() * 2)) return;
                const auto groupIndex = static_cast<size_t>((choice - 1) / 2);
                if (choice % 2 == 1) safeThis->openLibrarySearch(groups[groupIndex]);
                else safeThis->deleteLibraryGroup(groupIndex);
            });
    }
    void deleteLibraryGroup(size_t index) {
        if (index >= libraryGroups.size()) return;
        const auto previousGroups = libraryGroups;
        const auto removedName = libraryGroups[index].name;
        libraryGroups.erase(libraryGroups.begin() + static_cast<std::ptrdiff_t>(index));
        if (!saveLibraryCatalog()) {
            libraryGroups = previousGroups;
            status.setText(utf8("Não foi possível excluir o agrupamento do catálogo local."),juce::dontSendNotification);
            return;
        }
        status.setText(utf8("Agrupamento excluído: ") + removedName + utf8(" · as fontes e outros grupos foram preservados."),juce::dontSendNotification);
        refreshBank(bankSelect.getSelectedId() - 1,false);
    }
    void previewLibraryCandidate() {
        if (!libraryCandidateSong || libraryCandidateCell < 0 || libraryCandidateCell >= libraryCandidateSong->cellCount) return;
        if (songSnapshots.size() >= 64) {
            status.setText(utf8("Limite de 64 reproduções nesta sessão; reinicie a maquete."),juce::dontSendNotification);
            return;
        }
        auto preview = std::make_unique<risco::Song>();
        preview->bpm = song.bpm; preview->beatsPerBar = song.beatsPerBar;
        preview->compoundSixEight = song.compoundSixEight; preview->bitDepth = song.bitDepth;
        preview->timbreMode = song.timbreMode; preview->ayNoisePeriod = song.ayNoisePeriod;
        preview->ayEnvelopeMode = song.ayEnvelopeMode; preview->ayEnvelopeRamp = song.ayEnvelopeRamp;
        preview->historicalAyTexture = song.historicalAyTexture;
        preview->historicalAyNoisePeriod = song.historicalAyNoisePeriod;
        preview->historicalAyEnvelope = song.historicalAyEnvelope;
        preview->historicalAyEnvelopeRamp = song.historicalAyEnvelopeRamp;
        preview->bank[0] = libraryCandidateSong->bank[static_cast<size_t>(libraryCandidateCell)];
        preview->cellCount = 1;
        risco::Placement placement{preview->bank[0].id,0,
            cellSpan.getSelectedId() == 1 ? risco::CellSpan::beat :
            cellSpan.getSelectedId() == 4 ? risco::CellSpan::twoBars :
            cellSpan.getSelectedId() == 3 ? risco::CellSpan::fourBars : risco::CellSpan::bar,1};
        placement.stem = pendingStem;
        preview->grid[0] = placement; preview->placementCount = 1;
        auto next = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*preview));
        next->applyStemAutomation = false;
        if (next->count == 0) {
            status.setText(utf8("Não foi possível preparar o candidato externo."),juce::dontSendNotification);
            return;
        }
        const auto* pointer = next.get();
        songSnapshots.push_back(std::move(next));
        previewingCell = true; playing.store(false,std::memory_order_release);
        directEnabled.store(false,std::memory_order_release); directMode.setToggleState(false,juce::dontSendNotification);
        autoMode.setToggleState(false,juce::dontSendNotification); active.store(nullptr,std::memory_order_release);
        activeSong.store(pointer,std::memory_order_release); restart.store(true,std::memory_order_release);
        playing.store(true,std::memory_order_release);
        status.setText(utf8("Pré-escuta externa · ainda não importada. Use Banco → Importar candidato externo para confirmar."),
            juce::dontSendNotification);
    }
    void importLibraryCandidate() {
        if (!libraryCandidateSong || libraryCandidateCell < 0 || libraryCandidateCell >= libraryCandidateSong->cellCount) return;
        const int selected = selectedCellIndex();
        const auto id = risco::importCell(song,libraryCandidateSong->bank[static_cast<size_t>(libraryCandidateCell)],
            libraryCandidateDescription);
        if (id == 0) {
            status.setText(utf8("Importação recusada: banco cheio, célula inválida ou histórico de origem excedido."),
                juce::dontSendNotification);
            return;
        }
        bankQuery.clear(); bankFavoritesOnly = false;
        libraryCandidateSong.reset(); libraryCandidateCell = -1; libraryCandidateDescription.clear();
        refreshBank(selected,false);
        status.setText(utf8("Célula externa importada no banco #")+juce::String(id)+
            utf8(" · não foi inserida na grade; origem disponível em Proveniência."),juce::dontSendNotification);
    }
    void finishLibraryRead() {
        if (!libraryResult || !libraryResult->done.load(std::memory_order_acquire)) return;
        auto result = std::move(libraryResult);
        if (result->song) {
            ProjectLibraryEntry entry;
            entry.song = std::shared_ptr<risco::Song>(std::move(result->song));
            entry.description = result->description;
            entry.displayName = libraryActiveFile.getFileName().toStdString();
            entry.canonicalPath = libraryActiveFile.getFullPathName().toStdString();
            libraryIndex.push_back(std::move(entry));
        } else {
            libraryReadErrors.push_back(libraryActiveFile.getFileName()+": "+juce::String::fromUTF8(result->error.c_str()));
        }
        libraryActiveFile = juce::File();
        if (!libraryPendingFiles.empty()) { startNextLibraryRead(); return; }
        const auto errorText = libraryReadErrors.empty() ? juce::String() :
            utf8(" · ") + juce::String(libraryReadErrors.size()) + utf8(" arquivo(s) recusado(s)");
        const auto catalogWarning = libraryCatalogSaveFailed ? utf8(" · lista local não salva") : juce::String();
        status.setText(juce::String(libraryIndex.size()) + utf8(" projeto(s) no índice temporário") + errorText + catalogWarning,
            juce::dontSendNotification);
        libraryReadErrors.clear();
        libraryCatalogSaveFailed = false;
        refreshBank(bankSelectionBeforeMenu,false);
        openLibrarySearch();
    }
    void refreshBank(int selectedIndex, bool loadSelection = true) {
        bankSelect.clear(juce::dontSendNotification);
        bankSelectionBeforeMenu = selectedIndex;
        int shown = 0;
        for (int i = 0; i < song.cellCount; ++i) {
            const auto& cell = song.bank[static_cast<size_t>(i)];
            auto label = juce::String::fromUTF8(cell.name.data(), int(cell.name.size()));
            const auto searchable = label + " " + juce::String(cell.id) + " " +
                juce::String::fromUTF8(cell.formulaExpression.c_str()) + " " +
                juce::String::fromUTF8(cell.formulaOrigin.author.c_str()) + " " +
                juce::String::fromUTF8(cell.formulaOrigin.source.c_str()) + " " + juce::String::fromUTF8(cell.importProvenance.c_str());
            if ((bankFavoritesOnly && !cell.favorite) || !searchable.containsIgnoreCase(bankQuery)) continue;
            ++shown;
            if (label.isEmpty()) label = utf8("Célula");
            if (cell.favorite) label = "* " + label;
            if (cell.formulaEventPhrase) label += utf8(" · notas");
            bankSelect.addItem(label + " #" + juce::String(cell.id), i + 1);
        }
        bankSelect.addSeparator();
        bankSelect.addItem(utf8("Buscar…"), 1000);
        bankSelect.addItem(bankFavoritesOnly ? utf8("Só favoritas: ON") : utf8("Só favoritas: OFF"), 1001);
        bankSelect.addItem(utf8("Favoritar/desfavoritar seleção"), 1002);
        bankSelect.setItemEnabled(1002, selectedIndex >= 0 && selectedIndex < song.cellCount);
        bankSelect.addItem(utf8("Mostrar todas"), 1003);
        bankSelect.addItem(utf8("Proveniência da seleção"), 1004);
        bankSelect.addItem(utf8("Indexar projetos externos…"), 1005);
        bankSelect.addItem(utf8("Buscar no índice externo · ")+juce::String(libraryIndex.size())+utf8(" projeto(s)"),1006);
        bankSelect.setItemEnabled(1006,!libraryIndex.empty());
        bankSelect.addItem(utf8("Ouvir candidato externo"),1007);
        bankSelect.addItem(utf8("Importar candidato externo"),1008);
        bankSelect.setItemEnabled(1007,libraryCandidateSong != nullptr && libraryCandidateCell >= 0);
        bankSelect.setItemEnabled(1008,libraryCandidateSong != nullptr && libraryCandidateCell >= 0);
        bankSelect.addItem(utf8("Limpar índice externo da sessão"),1009);
        bankSelect.setItemEnabled(1009,!libraryIndex.empty());
        bankSelect.addItem(utf8("Reindexar catálogo salvo · ") + juce::String(librarySourcePaths.size()),1010);
        bankSelect.setItemEnabled(1010,!librarySourcePaths.isEmpty());
        bankSelect.addItem(utf8("Limpar catálogo salvo"),1011);
        bankSelect.setItemEnabled(1011,!librarySourcePaths.isEmpty() || libraryCatalogFile().existsAsFile());
        bankSelect.addItem(utf8("Buscar por agrupamento autoral · ") + juce::String(libraryGroups.size()),1012);
        bankSelect.setItemEnabled(1012,!libraryGroups.empty() && !libraryIndex.empty());
        bankSelect.addItem(utf8("Criar agrupamento autoral…"),1013);
        bankSelect.setItemEnabled(1013,libraryGroups.size() < 16 && !libraryResult && libraryPendingFiles.empty());
        bankSelect.setItemEnabled(1004, selectedIndex >= 0 && selectedIndex < song.cellCount);
        bankSelect.setTextWhenNothingSelected(juce::String(shown) + utf8(" células · filtro"));
        if (song.cellCount > 0) {
            const int selected = std::clamp(selectedIndex, 0, song.cellCount - 1);
            if (bankSelect.indexOfItemId(selected + 1) >= 0)
                bankSelect.setSelectedId(selected + 1, juce::dontSendNotification);
            roleSelect.setSelectedId(int(song.bank[static_cast<size_t>(selected)].role) + 1,
                                     juce::dontSendNotification);
            roleSelect.setEnabled(true);
            if (loadSelection) loadSelectedCell();
        } else {
            roleSelect.setSelectedId(1, juce::dontSendNotification);
            roleSelect.setEnabled(false);
            inscription = {};
            ascii.clear();
            cellName.clear();
            repaint();
        }
    }
    void loadSelectedCell() {
        const int index = selectedCellIndex();
        if (index < 0) return;
        previewCellButton.setTooltip(utf8("Toca somente a célula escolhida no banco; se seu bloco está selecionado na grade, ouve o trecho recortado."));
        if (activeSong.load(std::memory_order_acquire) == nullptr) {
            playing.store(false, std::memory_order_release);
            active.store(nullptr, std::memory_order_release);
            directEnabled.store(false, std::memory_order_release);
            directMode.setToggleState(false, juce::dontSendNotification);
        }
        const auto& cell = song.bank[static_cast<size_t>(index)];
        bankSelectionBeforeMenu = index;
        inputInterpretationChoice = cell.inputInterpretation;
        inputInterpretationStrength = cell.inputInterpretationStrength;
        inputGestureCurve = cell.inputGestureCurve;
        inputGestureRangeSemitones = cell.inputGestureRangeSemitones;
        roleSelect.setSelectedId(int(cell.role) + 1, juce::dontSendNotification);
        if (cell.bytebeatFormula) {
            currentPhraseGenerated = false;
            formulaInput.setText(juce::String::fromUTF8(cell.formulaExpression.c_str()),
                                 juce::dontSendNotification);
            cellName.setText(juce::String::fromUTF8(cell.name.c_str()));
            if (cell.formulaEventPhrase) {
                formulaProgressionChoice = cell.formulaProgression;
                formulaHarmonicBarsChoice = cell.formulaHarmonicBars;
                formulaSwingChoice = cell.formulaSwing;
                formulaSubdivisionChoice = cell.formulaSubdivision;
                formulaSyncopationChoice = cell.formulaSyncopation;
                formulaPulsesChoice = cell.formulaPulsesPerBar;
                formulaChordRootChoices = cell.formulaChordRoots;
                formulaScaleSelect.setSelectedId(int(cell.formulaScale) + 1,
                                                 juce::dontSendNotification);
                formulaTonicSelect.setSelectedId(int(cell.formulaTonic) + 1,
                                                 juce::dontSendNotification);
                formulaMotionSelect.setSelectedId(int(cell.formulaMotion) + 1,
                                                  juce::dontSendNotification);
                status.setText(utf8("Frase fórmula selecionada · escala, tom e gesto restaurados") +
                    (cell.formulaMotion == risco::FormulaScaleMotion::accompany || cell.formulaMotion == risco::FormulaScaleMotion::chordGuide
                        ? utf8(" · ") + harmonicPlanLabel() : juce::String()) +
                    swingLabel() + utf8(". Use Ouvir célula para escutá-la."),
                               juce::dontSendNotification);
            } else {
                status.setText(utf8("Fórmula selecionada. Use Ouvir célula para escutá-la."),
                               juce::dontSendNotification);
            }
            if (cell.formulaOrigin.source == "risco:glitch-math-v1") {
                const auto parent = juce::String(cell.formulaOrigin.parentA);
                status.setText(utf8("Glitch derivado do pai #") + parent +
                    utf8(". Ouvir célula testa o glitch; selecione #") + parent +
                    utf8(" no banco para retomar o original."), juce::dontSendNotification);
                previewCellButton.setTooltip(utf8("Ouve este glitch isolado. Para voltar à fórmula original, selecione no banco a célula-pai #") + parent + utf8(" e use Ouvir célula."));
            }
        } else {
            inscription = cell.inscription;
            ascii.setText(juce::String::fromUTF8(inscription.ascii.data(), int(inscription.ascii.size())),
                          juce::dontSendNotification);
            marqueeScrollStartMillis = juce::Time::getMillisecondCounter();
            currentPhraseGenerated = cell.generatedPhrase;
            if (!cell.generatedPhrase)
                setTextEntryMode(inscription.count == 0, false);
            cellName.setText(juce::String::fromUTF8(cell.name.data(), int(cell.name.size())));
        }
        if (!cell.bytebeatFormula && cell.generatedPhrase) {
            currentPhraseSeed = cell.phraseSeed;
            currentPhraseVariation = cell.phraseVariation;
            currentPhraseVersion = cell.phraseVersion;
            currentPhraseSectionMask = cell.phraseSectionMask;
            currentSectionEdits = cell.sectionEdits;
            currentPhraseMeters = cell.phraseMeters;
            currentPhraseSectionBars = cell.phraseSectionBars;
            compareOriginalSections = false;
            selectedPhraseVersion = cell.phraseVersion;
            phraseVariation = cell.phraseVariation + 1;
            const auto formName = cell.phraseVersion == 4 ? utf8("A–B · dois compassos") :
                                  cell.phraseVersion == 3 ? "A–B–A–C" :
                                  cell.phraseVersion == 9 ? utf8("Espiral A–B–C–A′ · métrica nativa") :
                                  cell.phraseVersion == 8 ? utf8("Arco A–A′–ápice–retorno · métrica nativa") :
                                  cell.phraseVersion == 7 ? utf8("Mapa logístico · percurso com estado") :
                                  cell.phraseVersion == 6 ? utf8("A–B–A–C · métrica nativa") :
                                  cell.phraseVersion == 5 ? utf8("A–B–A–C · arco dinâmico") :
                                                            utf8("A–A′–contraste–retorno");
            status.setText(utf8("Frase V") + juce::String(cell.phraseVersion) + " · " +
                               formName +
                               activePhraseSectionsLabel() +
                               phraseDynamicsLabel() +
                               utf8(" · Use Ouvir célula para escutá-la."),
                           juce::dontSendNotification);
        } else if (!cell.bytebeatFormula)
            status.setText(utf8("Célula selecionada. Use Ouvir célula para escutá-la."),
                           juce::dontSendNotification);
        repaint();
    }
    void storeCurrentPhraseTake() {
        if (!currentPhraseGenerated) return;
        risco::Cell recipe;
        const int selected = selectedCellIndex();
        if (selected >= 0) {
            const auto& source = song.bank[static_cast<size_t>(selected)];
            if (source.generatedPhrase && source.phraseSeed == currentPhraseSeed) recipe = source;
        }
        recipe.generatedPhrase = true;
        recipe.inscription.ascii = "GEN";
        recipe.name = "Take #" + std::to_string(song.nextCellId) + " · V" + std::to_string(currentPhraseVersion);
        recipe.phraseSeed = currentPhraseSeed;
        recipe.phraseVariation = currentPhraseVariation;
        recipe.phraseVersion = currentPhraseVersion;
        recipe.phraseSectionMask = currentPhraseSectionMask;
        recipe.sectionEdits = compareOriginalSections ? risco::SectionEdits{} : currentSectionEdits;
        recipe.phraseMeters = effectivePhraseMeters();
        recipe.phraseSectionBars = effectivePhraseSectionBars();
        const auto sequence = phraseForCurrentMeter(currentPhraseSeed,currentPhraseVariation,currentPhraseVersion,currentPhraseSectionMask);
        recipe.waveform = captureWaveform(sequence);
        recipe.waveformCount = risco::Cell::waveformSize;
        const auto id = risco::storePhraseTake(song,recipe);
        if (id == 0) {
            status.setText(utf8("Não foi possível guardar o take: banco cheio ou receita inválida."),juce::dontSendNotification);
            return;
        }
        bankQuery.clear(); bankFavoritesOnly = false;
        refreshBank(song.cellCount - 1);
        status.setText(utf8("Take #") + juce::String(id) +
            utf8(" guardado no banco · original e grade preservados. Salvar conserva versões e preferência."),juce::dontSendNotification);
    }
    void storeCurrentCell() {
        if (directEnabled.load(std::memory_order_acquire) &&
            activeFormula.load(std::memory_order_acquire) != nullptr) {
            const auto* program = activeFormula.load(std::memory_order_acquire);
            auto name = cellName.getText().toStdString();
            if (name.empty()) name = "Bytebeat " + std::to_string(song.cellCount + 1);
            const auto id = risco::storeFormulaCell(song, formulaInput.getText().toStdString(),
                std::move(name), captureFormulaWaveform(*program,
                    displayedDirectTime.load(std::memory_order_relaxed)));
            if (id == 0) {
                status.setText(utf8("Banco cheio ou fórmula inválida."), juce::dontSendNotification);
                return;
            }
            auto& stored = song.bank[static_cast<size_t>(song.cellCount - 1)];
            stored.role = autoRouteButton.getToggleState()
                ? generativeRoleForCell(stored, routingSeedForCell(stored))
                : song.stemRole[static_cast<size_t>(std::clamp(pendingStem, 0, risco::Song::stemCount - 1))];
            const bool archived = archiveFormula(song.bank[static_cast<size_t>(song.cellCount - 1)]);
            refreshBank(song.cellCount - 1, false);
            status.setText(archived ? utf8("Fórmula e onda guardadas no banco e no repositório.")
                                    : utf8("Fórmula guardada no banco; repositório local indisponível."),
                           juce::dontSendNotification);
            repaint();
            return;
        }
        if (!currentPhraseGenerated && !syncInscriptionText()) return;
        auto name = cellName.getText().toStdString();
        if (name.empty()) name = currentPhraseGenerated
            ? "Frase V" + std::to_string(currentPhraseVersion) + " #" +
                  std::to_string(song.cellCount + 1)
            : "Celula " + std::to_string(song.cellCount + 1);
        std::uint32_t id = 0;
        if (currentPhraseGenerated) {
            const auto generated = phraseForCurrentMeter(
                currentPhraseSeed, currentPhraseVariation, currentPhraseVersion, currentPhraseSectionMask);
            id = risco::storeGeneratedCell(song, currentPhraseSeed, currentPhraseVariation,
                                           std::move(name), captureWaveform(generated), currentPhraseVersion,
                                           currentPhraseSectionMask);
            if (id != 0) song.bank[static_cast<size_t>(song.cellCount - 1)].sectionEdits =
                compareOriginalSections ? risco::SectionEdits{} : currentSectionEdits;
            if (id != 0) song.bank[static_cast<size_t>(song.cellCount - 1)].phraseMeters = effectivePhraseMeters();
            if (id != 0) song.bank[static_cast<size_t>(song.cellCount - 1)].phraseSectionBars = effectivePhraseSectionBars();
        } else {
            id = risco::storeCell(song, activeInput(), std::move(name));
            if (id != 0) {
                song.bank[static_cast<size_t>(song.cellCount - 1)].inputInterpretation = inputInterpretationChoice;
                song.bank[static_cast<size_t>(song.cellCount - 1)].inputInterpretationStrength = inputInterpretationStrength;
                song.bank[static_cast<size_t>(song.cellCount - 1)].inputGestureCurve = inputGestureCurve;
                song.bank[static_cast<size_t>(song.cellCount - 1)].inputGestureRangeSemitones = inputGestureRangeSemitones;
            }
        }
        if (id == 0) {
            status.setText(utf8("Banco cheio ou célula inválida."), juce::dontSendNotification);
            return;
        }
        auto& stored = song.bank[static_cast<size_t>(song.cellCount - 1)];
        stored.role = autoRouteButton.getToggleState()
            ? generativeRoleForCell(stored, routingSeedForCell(stored))
            : song.stemRole[static_cast<size_t>(std::clamp(pendingStem, 0, risco::Song::stemCount - 1))];
        refreshBank(song.cellCount - 1);
        status.setText(autoRouteButton.getToggleState()
            ? utf8("Célula guardada · AUTO definiu o papel musical. Inserir célula deixa o Risco escolher tempo e canal.")
            : utf8("Célula guardada com papel do Stem ") + juce::String(pendingStem + 1) +
                  utf8(". Clique na grade para inserir no canal escolhido."),
                       juce::dontSendNotification);
    }
    void updateSelectedCell() {
        const int index = selectedCellIndex();
        if (index >= 0 && song.bank[static_cast<size_t>(index)].bytebeatFormula) {
            status.setText(utf8("Guarde a expressão editada como nova célula."),
                           juce::dontSendNotification);
            return;
        }
        if (index < 0) return;
        if (currentPhraseGenerated) {
            auto& cell = song.bank[static_cast<size_t>(index)];
            if (!cell.generatedPhrase) {
                status.setText(utf8("Use Guardar célula para criar uma frase sem substituir esta inscrição."),
                               juce::dontSendNotification);
                return;
            }
            auto name = cellName.getText().toStdString();
            const unsigned effectiveMask = currentPhraseVersion == 4 ? 0x03u : 0x0fu;
            if (name.size() > 128 || name.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
                cell.revision == std::numeric_limits<std::uint32_t>::max() ||
                (currentPhraseSectionMask & effectiveMask) == 0) {
                status.setText(utf8("Nome inválido, revisão no limite ou frase sem seções ativas."),
                               juce::dontSendNotification);
                return;
            }
            const auto phrase = phraseForCurrentMeter(currentPhraseSeed, currentPhraseVariation,
                currentPhraseVersion, currentPhraseSectionMask);
            const auto waveform = captureWaveform(phrase);
            pushBankUndo(cell);
            cell.name = std::move(name);
            cell.phraseSeed = currentPhraseSeed;
            cell.phraseVariation = currentPhraseVariation;
            cell.phraseVersion = currentPhraseVersion;
            cell.phraseSectionMask = currentPhraseSectionMask;
            cell.sectionEdits = compareOriginalSections ? risco::SectionEdits{} : currentSectionEdits;
            cell.phraseMeters = effectivePhraseMeters();
            cell.phraseSectionBars = effectivePhraseSectionBars();
            cell.waveform = waveform;
            cell.waveformCount = risco::Cell::waveformSize;
            ++cell.revision;
            refreshBank(index);
            refreshPlaybackAfterGridEdit();
            status.setText(utf8("Frase atualizada em todas as ocorrências · revisão ") +
                juce::String(cell.revision) + activePhraseSectionsLabel(), juce::dontSendNotification);
            return;
        }
        if (!syncInscriptionText()) return;
        auto before = std::make_unique<risco::Cell>(song.bank[static_cast<size_t>(index)]);
        auto name = cellName.getText().toStdString();
        if (name.size() > 128 || name.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
            !risco::updateCell(song, song.bank[static_cast<size_t>(index)].id, activeInput())) {
            status.setText(utf8("Não foi possível atualizar a célula."), juce::dontSendNotification);
            return;
        }
        song.bank[static_cast<size_t>(index)].name = std::move(name);
        song.bank[static_cast<size_t>(index)].inputInterpretation = inputInterpretationChoice;
        song.bank[static_cast<size_t>(index)].inputInterpretationStrength = inputInterpretationStrength;
        song.bank[static_cast<size_t>(index)].inputGestureCurve = inputGestureCurve;
        song.bank[static_cast<size_t>(index)].inputGestureRangeSemitones = inputGestureRangeSemitones;
        pushBankUndo(*before);
        refreshBank(index);
        refreshPlaybackAfterGridEdit();
        status.setText(utf8("Célula atualizada em todas as ocorrências."), juce::dontSendNotification);
    }
    void pushBankUndo(const risco::Cell& cell) {
        if (bankUndo.size() == 8) bankUndo.erase(bankUndo.begin());
        bankUndo.push_back(std::make_unique<risco::Cell>(cell));
        bankRedo.clear();
    }
    void restoreBankEdit(bool redo) {
        auto& from = redo ? bankRedo : bankUndo;
        auto& to = redo ? bankUndo : bankRedo;
        if (from.empty()) return;
        const auto id = from.back()->id;
        for (int i = 0; i < song.cellCount; ++i) {
            auto& cell = song.bank[static_cast<size_t>(i)];
            if (cell.id != id) continue;
            if (to.size() == 8) to.erase(to.begin());
            to.push_back(std::make_unique<risco::Cell>(cell));
            cell = *from.back();
            from.pop_back();
            refreshBank(i);
            refreshPlaybackAfterGridEdit();
            status.setText((redo ? utf8("Edição do banco refeita · célula #") :
                                  utf8("Edição do banco desfeita · célula #")) +
                juce::String(id) + utf8(" · todas as ocorrências."), juce::dontSendNotification);
            return;
        }
        from.pop_back();
        status.setText(utf8("A célula desta edição não está mais no banco."), juce::dontSendNotification);
    }
    void openCompositionSeedDialog() {
        if (compositionVersions.size() >= 16) {
            status.setText(utf8("A árvore de versões está cheia; libere uma posição antes de gerar."),
                           juce::dontSendNotification);
            return;
        }
        compositionSeedDialog = std::make_unique<juce::AlertWindow>(
            utf8("Variação generativa da música"),
            utf8("Gera uma proposta numa cópia. A composição atual será guardada como versão antes de aplicar."),
            juce::MessageBoxIconType::NoIcon);
        compositionSeedDialog->addTextEditor("seed", song.compositionSeedRecipeEnabled
            ? juce::String(song.compositionSeed) : juce::String("20261123"), utf8("Seed inteira (0–2⁶⁴−1):"));
        if (auto* editor = compositionSeedDialog->getTextEditor("seed"))
            editor->setInputRestrictions(20, "0123456789");
        juce::StringArray levels;
        levels.add(utf8("Semente assistida · frase e andamento"));
        levels.add(utf8("Semente de conjunto · stems e diálogo"));
        levels.add(utf8("Semente ampla · timbres, FX e mixagem"));
        compositionSeedDialog->addComboBox("level", levels, utf8("Nível:"));
        compositionSeedDialog->getComboBoxComponent("level")->setSelectedId(1, juce::dontSendNotification);
        juce::StringArray lockProfiles;
        lockProfiles.add(utf8("Padrão · somente MASTER travado"));
        lockProfiles.add(utf8("Frases travadas"));
        lockProfiles.add(utf8("Andamento e forma travados"));
        lockProfiles.add(utf8("Stems e diálogo travados"));
        lockProfiles.add(utf8("Timbres e FX travados"));
        lockProfiles.add(utf8("Mixagem dos stems travada"));
        lockProfiles.add(utf8("Travar frases, andamento e stems"));
        lockProfiles.add(utf8("Travar todas as famílias"));
        compositionSeedDialog->addComboBox("locks", lockProfiles, utf8("Perfil de travas:"));
        compositionSeedDialog->getComboBoxComponent("locks")->setSelectedId(1, juce::dontSendNotification);
        compositionSeedDialog->addButton(uiText("Revisar proposta", "Review proposal", "Vérifier la proposition", "Revisar propuesta"), 1,
            juce::KeyPress(juce::KeyPress::returnKey));
        compositionSeedDialog->addButton(uiText("Cancelar", "Cancel", "Annuler", "Cancelar"), 0,
            juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        compositionSeedDialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis](int choice) {
                if (!safeThis || !safeThis->compositionSeedDialog) return;
                const auto seedText = safeThis->compositionSeedDialog->getTextEditorContents("seed").trim();
                const int levelId = safeThis->compositionSeedDialog->getComboBoxComponent("level")->getSelectedId();
                const int lockProfile = safeThis->compositionSeedDialog->getComboBoxComponent("locks")->getSelectedId();
                std::uint32_t locks = risco::compositionSeedDefaultLocks;
                const auto phraseLock = risco::compositionSeedLock(risco::CompositionSeedLock::phrases);
                const auto tempoLock = risco::compositionSeedLock(risco::CompositionSeedLock::tempoAndForm);
                const auto ensembleLock = risco::compositionSeedLock(risco::CompositionSeedLock::ensemble);
                switch (lockProfile) {
                    case 2: locks |= phraseLock; break;
                    case 3: locks |= tempoLock; break;
                    case 4: locks |= ensembleLock; break;
                    case 5: locks |= risco::compositionSeedLock(risco::CompositionSeedLock::timbreAndFx); break;
                    case 6: locks |= risco::compositionSeedLock(risco::CompositionSeedLock::stemMix); break;
                    case 7: locks |= phraseLock | tempoLock | ensembleLock; break;
                    case 8: locks = risco::compositionSeedKnownLocks; break;
                    default: break;
                }
                safeThis->compositionSeedDialog.reset();
                if (choice != 1) return;
                std::uint64_t seed = 0;
                const auto seedUtf8 = seedText.toStdString();
                const auto parsed = std::from_chars(seedUtf8.data(), seedUtf8.data() + seedUtf8.size(), seed);
                if (seedUtf8.empty() || parsed.ec != std::errc{} || parsed.ptr != seedUtf8.data() + seedUtf8.size() ||
                    levelId < 1 || levelId > 3) {
                    safeThis->status.setText(utf8("Seed ou nível inválido; nenhuma alteração foi aplicada."),
                                             juce::dontSendNotification);
                    return;
                }
                auto candidate = std::make_shared<risco::Song>();
                if (!risco::composeSongVariation(safeThis->song, seed,
                        static_cast<risco::CompositionSeedLevel>(levelId), locks, *candidate)) {
                    safeThis->showActivity(utf8("Variação por seed falhou"));
                    safeThis->status.setText(utf8("Não foi possível gerar a proposta; o projeto continua intacto."),
                                             juce::dontSendNotification);
                    return;
                }
                safeThis->reviewCompositionSeedCandidate(std::move(candidate));
            }), false);
    }
    void openAlbumDialog() {
        if ((albumJob && albumJob->isThreadRunning()) ||
            (albumRenderJob && albumRenderJob->isThreadRunning())) return;
        albumDialog = std::make_unique<juce::AlertWindow>(
            uiText("Planejar álbum generativo", "Plan generative album", "Planifier un album génératif", "Planificar álbum generativo"),
            uiText("Deriva faixas completas e independentes. Nesta etapa, os projetos .risco e o manifesto serão criados; WAVs serão uma etapa posterior.",
                   "Derives complete, independent tracks. This step creates the .risco projects and manifest; WAV rendering comes later.",
                   "Dérive des morceaux complets et indépendants. Cette étape crée les projets .risco et le manifeste ; le rendu WAV viendra ensuite.",
                   "Deriva piezas completas e independientes. Este paso crea los proyectos .risco y el manifiesto; el render WAV vendrá después."),
            juce::MessageBoxIconType::NoIcon);
        albumDialog->addTextEditor("title", uiText("Núcleo", "Core", "Noyau", "Núcleo"),
                                   uiText("Título do álbum:", "Album title:", "Titre de l’album :", "Título del álbum:"));
        albumDialog->getTextEditor("title")->setInputRestrictions(128);
        std::array<risco::AlbumSource, 3> sources{};
        const auto text = ascii.getText().trim().toStdString();
        if (!text.empty()) {
            sources[1] = {risco::SeedTrackMaterial::text, risco::formulaSeedFromText(text),
                          risco::seedTrackTextCue(text), risco::seedTrackMotif(risco::composeText(text))};
        }
        if (inscription.count > 0) {
            sources[2] = {risco::SeedTrackMaterial::drawing, risco::formulaSeedFromDrawing(inscription),
                          risco::seedTrackDrawingCue(inscription), risco::seedTrackMotif(risco::compose(inscription))};
        }
        albumDialog->addComboBox("source", {
            uiText("Número", "Number", "Nombre", "Número"),
            uiText("Texto atual", "Current text", "Texte actuel", "Texto actual"),
            uiText("Desenho atual", "Current drawing", "Dessin actuel", "Dibujo actual")},
            uiText("Origem:", "Source:", "Source :", "Origen:"));
        auto* sourceChoice = albumDialog->getComboBoxComponent("source");
        for (int i = 1; i < int(sources.size()); ++i)
            sourceChoice->setItemEnabled(i + 1, sources[static_cast<std::size_t>(i)].motif.count > 0);
        const int selectedSource = seedSource.getSelectedId();
        sourceChoice->setSelectedId(selectedSource >= 2 && selectedSource <= 3 &&
            sourceChoice->isItemEnabled(selectedSource) ? selectedSource : 1, juce::dontSendNotification);
        albumDialog->addTextEditor("seed", "20261123", uiText("Seed-mãe (1–2⁶⁴−1):", "Root seed (1–2⁶⁴−1):", "Seed mère (1–2⁶⁴−1) :", "Seed madre (1–2⁶⁴−1):"));
        auto* seedEditor = albumDialog->getTextEditor("seed");
        seedEditor->setInputRestrictions(20, "0123456789");
        sourceChoice->onChange = [sourceChoice, seedEditor, sources, previous = 1,
                                   values = std::array<juce::String, 3>{"20261123", {}, {}}]() mutable {
            values[static_cast<std::size_t>(previous - 1)] = seedEditor->getText();
            previous = std::clamp(sourceChoice->getSelectedId(), 1, 3);
            const auto index = static_cast<std::size_t>(previous - 1);
            const auto suggestion = juce::String(sources[index].fingerprint);
            seedEditor->setText(values[index].isEmpty() && previous > 1 ? suggestion : values[index], false);
        };
        sourceChoice->onChange();
        juce::StringArray countChoices;
        for (int count = 2; count <= risco::AlbumPlan::maxTracks; ++count)
            countChoices.add(juce::String(count));
        albumDialog->addComboBox("count", countChoices, uiText("Número de faixas:", "Number of tracks:", "Nombre de morceaux :", "Número de piezas:"));
        albumDialog->getComboBoxComponent("count")->setSelectedId(7, juce::dontSendNotification);
        albumDialog->addTextEditor("bpm", "110", uiText("BPM central (40–240):", "Center BPM (40–240):", "BPM central (40–240) :", "BPM central (40–240):"));
        albumDialog->getTextEditor("bpm")->setInputRestrictions(6, "0123456789.");
        albumDialog->addTextEditor("spread", "20", uiText("Amplitude do contorno (0–40 BPM):", "Tempo contour range (0–40 BPM):", "Amplitude du contour (0–40 BPM) :", "Amplitud del contorno (0–40 BPM):"));
        albumDialog->getTextEditor("spread")->setInputRestrictions(5, "0123456789.");
        juce::StringArray contours;
        contours.add(uiText("Equilibrado", "Balanced", "Équilibré", "Equilibrado"));
        contours.add(uiText("Contrastante", "Contrasting", "Contrasté", "Contrastante"));
        contours.add(uiText("Ascendente", "Rising", "Ascendant", "Ascendente"));
        contours.add(uiText("Arco", "Arc", "Arc", "Arco"));
        albumDialog->addComboBox("contour", contours, uiText("Contorno de andamento:", "Tempo contour:", "Contour de tempo :", "Contorno de tempo:"));
        albumDialog->getComboBoxComponent("contour")->setSelectedId(4, juce::dontSendNotification);
        albumDialog->addComboBox("profile", {
            uiText("Original · retomar V1", "Original · resume V1", "Original · reprendre V1", "Original · retomar V1"),
            uiText("Parentesco · paletas recorrentes", "Related · recurring palettes", "Parenté · palettes récurrentes", "Afinidad · paletas recurrentes"),
            uiText("Contraste · alterna formas, ritmos e timbres", "Contrasting · varies form, rhythm and timbre", "Contraste · alterne formes, rythmes et timbres", "Contraste · alterna formas, ritmos y timbres")},
            uiText("Percurso musical:", "Musical profile:", "Parcours musical :", "Recorrido musical:"));
        albumDialog->getComboBoxComponent("profile")->setSelectedId(3, juce::dontSendNotification);
        albumDialog->addComboBox("arrangement", {
            uiText("Por partes", "By parts", "Par sections", "Por secciones"),
            uiText("Contínuo", "Continuous", "Continu", "Continuo")},
            uiText("Arranjo:", "Arrangement:", "Arrangement :", "Arreglo:"));
        albumDialog->getComboBoxComponent("arrangement")->setSelectedId(1, juce::dontSendNotification);
        albumDialog->addTextEditor("trackProfiles", "", uiText(
            "Perfis por faixa · O original, R parentesco, C contraste, - perfil geral",
            "Per-track profiles · O original, R related, C contrasting, - album default",
            "Profils par morceau · O original, R parenté, C contraste, - profil général",
            "Perfiles por pieza · O original, R afinidad, C contraste, - perfil general"));
        albumDialog->getTextEditor("trackProfiles")->setInputRestrictions(23, "ORCorc- ,");
        albumDialog->addButton(uiText("Revisar plano", "Review plan", "Vérifier le plan", "Revisar plan"), 1,
            juce::KeyPress(juce::KeyPress::returnKey));
        albumDialog->addButton(uiText("Cancelar", "Cancel", "Annuler", "Cancelar"), 0,
            juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        albumDialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, sources](int choice) {
                if (!safeThis || !safeThis->albumDialog) return;
                const auto title = safeThis->albumDialog->getTextEditorContents("title").trim();
                const auto seedText = safeThis->albumDialog->getTextEditorContents("seed").trim().toStdString();
                const auto bpmText = safeThis->albumDialog->getTextEditorContents("bpm").trim();
                const auto spreadText = safeThis->albumDialog->getTextEditorContents("spread").trim();
                const int countId = safeThis->albumDialog->getComboBoxComponent("count")->getSelectedId();
                const int contourId = safeThis->albumDialog->getComboBoxComponent("contour")->getSelectedId();
                const int profileId = safeThis->albumDialog->getComboBoxComponent("profile")->getSelectedId();
                const int arrangementId = safeThis->albumDialog->getComboBoxComponent("arrangement")->getSelectedId();
                const int sourceId = safeThis->albumDialog->getComboBoxComponent("source")->getSelectedId();
                const auto trackProfilesText = safeThis->albumDialog->getTextEditorContents("trackProfiles");
                safeThis->albumDialog.reset();
                if (choice != 1) return;
                std::uint64_t seed = 0;
                const auto parsed = std::from_chars(seedText.data(), seedText.data() + seedText.size(), seed);
                const auto bpm = bpmText.getDoubleValue();
                const auto spread = spreadText.getDoubleValue();
                if (title.isEmpty() || seedText.empty() || bpmText.isEmpty() || spreadText.isEmpty() ||
                    parsed.ec != std::errc{} ||
                    parsed.ptr != seedText.data() + seedText.size() || seed == 0 ||
                    !std::isfinite(bpm) || !std::isfinite(spread) || countId < 1 ||
                    countId > risco::AlbumPlan::maxTracks - 1 || contourId < 1 || contourId > 4 ||
                    profileId < 1 || profileId > 3 || sourceId < 1 || sourceId > int(sources.size())) {
                    safeThis->status.setText(safeThis->uiText(
                        "Parâmetros inválidos; nenhum arquivo foi criado.",
                        "Invalid parameters; no files were created.",
                        "Paramètres invalides ; aucun fichier n’a été créé.",
                        "Parámetros inválidos; no se creó ningún archivo."), juce::dontSendNotification);
                    return;
                }
                risco::AlbumPlanRequest request;
                request.seed = seed;
                request.trackCount = countId + 1;
                request.centerBpm = bpm;
                request.tempoSpread = spread;
                request.contour = static_cast<risco::AlbumTempoContour>(contourId - 1);
                request.profile = static_cast<risco::AlbumMusicProfile>(profileId - 1);
                request.arrangeByParts = arrangementId == 1;
                request.source = sources[static_cast<std::size_t>(sourceId - 1)];
                request.generatorVersion = request.source.material == risco::SeedTrackMaterial::none ? 1 : 2;
                if (!parseAlbumTrackProfiles(trackProfilesText, request.trackCount, request.trackProfiles)) {
                    safeThis->status.setText(safeThis->uiText(
                        "Perfis inválidos. Use uma sequência O, R, C ou - (por exemplo: C,R,-,O).",
                        "Invalid profiles. Use a sequence of O, R, C or - (for example: C,R,-,O).",
                        "Profils invalides. Utilisez une suite O, R, C ou - (exemple : C,R,-,O).",
                        "Perfiles no válidos. Use una secuencia O, R, C o - (por ejemplo: C,R,-,O)."),
                        juce::dontSendNotification);
                    return;
                }
                risco::AlbumPlan plan;
                risco::AlbumManifest manifest;
                if (!risco::makeAlbumPlan(request, plan) ||
                    !risco::createAlbumManifest(plan, title.toStdString(), manifest)) {
                    safeThis->status.setText(safeThis->uiText(
                                                 "Plano fora dos limites do Álbum; revise BPM, amplitude e título.",
                                                 "Album plan is out of range; check BPM, range and title.",
                                                 "Le plan dépasse les limites de l’album ; vérifiez le BPM, l’amplitude et le titre.",
                                                 "El plan supera los límites del álbum; revise BPM, amplitud y título."),
                                             juce::dontSendNotification);
                    return;
                }
                safeThis->reviewAlbumPlan(std::move(manifest));
            }), false);
    }
    void reviewAlbumPlan(risco::AlbumManifest manifest) {
        double totalSeconds = 0.0;
        const juce::StringArray forms{
            uiText("Original V1", "Original V1", "Original V1", "Original V1"),
            uiText("Retorno", "Return", "Retour", "Retorno"),
            uiText("Colagem", "Collage", "Collage", "Collage")};
        const juce::StringArray pulses{
            uiText("reto", "straight", "droit", "recto"), "swing",
            uiText("recortado", "cut-up", "découpé", "recortado")};
        const juce::StringArray palettes{
            uiText("original", "original", "originale", "original"),
            uiText("ressonante", "resonant", "résonante", "resonante"),
            "AY", uiText("grão", "grain", "grain", "grano")};
        const juce::StringArray surprises{
            uiText("contida", "subtle", "contenue", "contenida"),
            uiText("média", "medium", "moyenne", "media"),
            uiText("alta", "high", "forte", "alta")};
        const juce::StringArray profiles{
            uiText("Original", "Original", "Original", "Original"),
            uiText("Parentesco", "Related", "Parenté", "Afinidad"),
            uiText("Contraste", "Contrasting", "Contraste", "Contraste")};
        const juce::StringArray motifVoices{
            uiText("melodia", "melody", "mélodie", "melodía"),
            uiText("baixo", "bass", "basse", "bajo"),
            uiText("textura", "texture", "texture", "textura")};
        const juce::StringArray motifReturns{
            uiText("retorno literal", "literal return", "retour littéral", "retorno literal"),
            uiText("retorno espelhado", "mirrored return", "retour en miroir", "retorno en espejo"),
            uiText("retorno espelhado + retrógrado", "mirrored + retrograde return",
                   "retour en miroir + rétrograde", "retorno en espejo + retrógrado")};
        juce::String sourceSummary;
        const auto material = manifest.plan.request.source.material;
        if (material != risco::SeedTrackMaterial::none)
            sourceSummary = (material == risco::SeedTrackMaterial::text
                ? uiText("Motivo do texto", "Text motif", "Motif du texte", "Motivo del texto")
                : uiText("Motivo do desenho", "Drawing motif", "Motif du dessin", "Motivo del dibujo")) +
                utf8(" · seed ") + juce::String(manifest.plan.request.seed) + "\n";
        juce::StringArray summaries;
        for (int index = 0; index < manifest.count; ++index) {
            const auto& track = manifest.tracks[static_cast<size_t>(index)];
            totalSeconds += track.plannedSeconds;
            const auto& plan = track.plan;
            auto summary = juce::String(index + 1).paddedLeft('0', 2) + "  " +
                profiles[static_cast<int>(plan.profile)] + " · " +
                juce::String(plan.bpm, 1) + " BPM / " + juce::String(plan.durationBars) +
                uiText(" compassos · ", " bars · ", " mesures · ", " compases · ") +
                forms[static_cast<int>(plan.form)];
            if (plan.form != risco::SeedTrackForm::original)
                summary += juce::String(" · ") + pulses[static_cast<int>(plan.pulse)] +
                    " · " + palettes[static_cast<int>(plan.palette)] +
                    " · " + uiText("surpresa ", "surprise ", "surprise ", "sorpresa ") + surprises[plan.surprise];
            if (plan.motifStem >= 0)
                summary += utf8(" · ") + uiText("motivo na pista ", "motif on ", "motif sur la piste ", "motivo en ") +
                    motifVoices[plan.motifStem / 2];
            if (plan.generatorVersion >= 2 && plan.motifStem >= 0)
                summary += utf8(" · ") + motifReturns[std::clamp(plan.surprise, 0, 2)];
            summaries.add(summary);
        }
        const auto minutes = totalSeconds / 60.0;
        showActivity(uiText("Plano de álbum · seed ", "Album plan · seed ", "Plan d’album · seed ", "Plan de álbum · seed ") + juce::String(manifest.plan.request.seed) +
                     " · " + juce::String(manifest.count) + uiText(" faixas · ", " tracks · ", " morceaux · ", " piezas · ") +
                     juce::String(minutes, 1) + uiText(" min previstos", " min estimated", " min estimées", " min estimados"));
        albumReviewDialog = std::make_unique<juce::AlertWindow>(
            uiText("Revisar plano do álbum", "Review album plan", "Vérifier le plan de l’album", "Revisar plan del álbum"),
            uiText("Título: ", "Title: ", "Titre : ", "Título: ") + juce::String::fromUTF8(manifest.title.c_str()) + "\n" +
            uiText("Faixas: ", "Tracks: ", "Morceaux : ", "Piezas: ") + juce::String(manifest.count) + uiText(" · duração prevista total: ", " · estimated total duration: ", " · durée totale estimée : ", " · duración total estimada: ") +
            juce::String(minutes, 1) + " min\n" +
            (manifest.plan.request.arrangeByParts
                ? uiText("Arranjo por partes em cada faixa", "Arrangement by parts in each track",
                         "Arrangement par sections dans chaque morceau", "Arreglo por secciones en cada pista")
                : uiText("Arranjo contínuo", "Continuous arrangement", "Arrangement continu", "Arreglo continuo")) +
            "\n" + sourceSummary + "\n" + summaries.joinIntoString("\n") +
            uiText("\n\nEsta etapa cria os projetos .risco. Em Álbum, use Renderizar WAVs para exportar o áudio.",
                   "\n\nThis step creates the .risco projects. Use Album → Render WAVs to export audio.",
                   "\n\nCette étape crée les projets .risco. Utilisez Album → Rendre les WAV pour exporter l’audio.",
                   "\n\nEste paso crea los proyectos .risco. Use Álbum → Renderizar WAVs para exportar el audio."),
            juce::MessageBoxIconType::QuestionIcon);
        albumReviewDialog->addButton(uiText("Escolher pasta e gerar", "Choose folder and generate", "Choisir un dossier et générer", "Elegir carpeta y generar"), 1,
            juce::KeyPress(juce::KeyPress::returnKey));
        albumReviewDialog->addButton(uiText("Cancelar", "Cancel", "Annuler", "Cancelar"), 0,
            juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        albumReviewDialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, manifest = std::move(manifest)](int choice) mutable {
                if (!safeThis) return;
                safeThis->albumReviewDialog.reset();
                if (choice != 1) return;
                safeThis->pendingAlbumManifest = std::make_unique<risco::AlbumManifest>(std::move(manifest));
                const auto initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
                safeThis->fileChooser = std::make_unique<juce::FileChooser>(
                    safeThis->uiText("Escolher pasta-pai para o álbum", "Choose parent folder for the album", "Choisir le dossier parent de l’album", "Elegir carpeta principal del álbum"), initial, "");
                safeThis->fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                    juce::FileBrowserComponent::canSelectDirectories,
                    [safeThis](const juce::FileChooser& chooser) {
                        if (!safeThis || chooser.getResult() == juce::File() ||
                            !safeThis->pendingAlbumManifest) return;
                        const auto parent = chooser.getResult();
                        if (!parent.isDirectory() || parent.getBytesFreeOnVolume() < 64 * 1024 * 1024) {
                            safeThis->status.setText(safeThis->uiText(
                                "Escolha uma pasta gravável com pelo menos 64 MiB livres.",
                                "Choose a writable folder with at least 64 MiB free.",
                                "Choisissez un dossier accessible en écriture avec au moins 64 Mio libres.",
                                "Elija una carpeta escribible con al menos 64 MiB libres."),
                                                     juce::dontSendNotification);
                            safeThis->pendingAlbumManifest.reset();
                            return;
                        }
                        const auto folderName = juce::File::createLegalFileName(
                            juce::String::fromUTF8(safeThis->pendingAlbumManifest->title.c_str()));
                        auto directory = parent.getChildFile(folderName);
                        auto manifest = std::move(*safeThis->pendingAlbumManifest);
                        safeThis->pendingAlbumManifest.reset();
                        if (safeThis->albumRenderJob && safeThis->albumRenderJob->isThreadRunning()) return;
                        if (directory.exists()) {
                            risco::AlbumManifest recovered;
                            bool valid = safeThis->readAlbumFolder(directory, recovered);
                            const auto& requested = manifest.plan;
                            const auto& saved = recovered.plan;
                            valid = valid && recovered.title == manifest.title &&
                                saved.request.seed == requested.request.seed &&
                                saved.request.trackCount == requested.request.trackCount &&
                                saved.request.centerBpm == requested.request.centerBpm &&
                                saved.request.tempoSpread == requested.request.tempoSpread &&
                                saved.request.contour == requested.request.contour &&
                                saved.request.profile == requested.request.profile &&
                                saved.request.trackProfiles == requested.request.trackProfiles &&
                                saved.request.arrangeByParts == requested.request.arrangeByParts &&
                                saved.request.source == requested.request.source &&
                                saved.count == requested.count;
                            for (int index = 0; valid && index < saved.count; ++index)
                                valid = saved.tracks[static_cast<size_t>(index)] ==
                                        requested.tracks[static_cast<size_t>(index)];
                            if (!valid) {
                                safeThis->status.setText(safeThis->uiText(
                                    "A pasta contém um álbum incompatível ou inválido; nenhum arquivo foi alterado.",
                                    "The folder contains an invalid or incompatible album; no files were changed.",
                                    "Le dossier contient un album invalide ou incompatible ; aucun fichier n’a été modifié.",
                                    "La carpeta contiene un álbum incompatible o no válido; no se modificó ningún archivo."),
                                                         juce::dontSendNotification);
                                return;
                            }
                            manifest = std::move(recovered);
                        } else if (!directory.createDirectory().wasOk()) {
                            safeThis->status.setText(safeThis->uiText(
                                "Não foi possível criar a pasta do álbum.",
                                "Could not create the album folder.",
                                "Impossible de créer le dossier de l’album.",
                                "No se pudo crear la carpeta del álbum."), juce::dontSendNotification);
                            return;
                        }
                        safeThis->startAlbumProjects(std::move(manifest), directory);
                    });
            }), false);
    }
    static bool readAlbumFolder(const juce::File& directory, risco::AlbumManifest& manifest) {
        if (!directory.isDirectory()) return false;
        const auto manifestFile = directory.getChildFile("album.risco-manifest");
        const auto size = manifestFile.getSize();
        juce::MemoryBlock bytes;
        if (size <= 0 || size > 1024 * 1024 || !manifestFile.loadFileAsData(bytes)) return false;
        std::istringstream input(std::string(static_cast<const char*>(bytes.getData()), bytes.getSize()));
        return risco::loadAlbumManifest(input, manifest);
    }

    void startAlbumProjects(risco::AlbumManifest manifest, const juce::File& directory) {
        if ((albumJob && albumJob->isThreadRunning()) ||
            (albumRenderJob && albumRenderJob->isThreadRunning())) return;
        if (directory.getBytesFreeOnVolume() < 64 * 1024 * 1024) {
            status.setText(uiText("São necessários 64 MiB livres na pasta do álbum.",
                                 "The album folder needs 64 MiB of free space.",
                                 "Le dossier de l’album nécessite 64 Mio d’espace libre.",
                                 "La carpeta del álbum necesita 64 MiB libres."), juce::dontSendNotification);
            return;
        }
        showActivity(uiText("Preparando ", "Preparing ", "Préparation de ", "Preparando ") +
            juce::String(manifest.count) + uiText(" projetos .risco · ", " .risco projects · ",
            " projets .risco · ", " proyectos .risco · ") + directory.getFileName());
        lastAlbumDirectory = directory;
        albumJob = std::make_unique<AlbumGenerationJob>(
            std::move(manifest), directory, languageSelect.getSelectedId() - 1);
        juce::Component::SafePointer<MainComponent> safeThis(this);
        albumJob->completed = [safeThis](juce::String result) {
            if (safeThis) safeThis->showActivity(result);
        };
        albumJob->launchThread(juce::Thread::Priority::low);
    }

    void openAlbumFolderChooser(bool resumeProjects = false) {
        if ((albumRenderJob && albumRenderJob->isThreadRunning()) ||
            (albumJob && albumJob->isThreadRunning())) return;
        const auto initial = lastAlbumDirectory.isDirectory() ? lastAlbumDirectory
            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        showActivity(uiText("Escolha a pasta que contém album.risco-manifest", "Choose the folder containing album.risco-manifest", "Choisissez le dossier contenant album.risco-manifest", "Elija la carpeta que contiene album.risco-manifest"));
        fileChooser = std::make_unique<juce::FileChooser>(
            uiText("Escolher pasta do álbum com album.risco-manifest", "Choose album folder with album.risco-manifest", "Choisir le dossier de l’album avec album.risco-manifest", "Elegir carpeta del álbum con album.risco-manifest"), initial, "", true, false, this);
        juce::Component::SafePointer<MainComponent> safeThis(this);
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [safeThis, resumeProjects](const juce::FileChooser& chooser) {
                if (!safeThis) return;
                if (chooser.getResult() == juce::File()) {
                    safeThis->showActivity(safeThis->uiText("Seleção de pasta cancelada", "Folder selection cancelled", "Sélection du dossier annulée", "Selección de carpeta cancelada"));
                    return;
                }
                const auto directory = chooser.getResult();
                risco::AlbumManifest manifest;
                bool valid = safeThis->readAlbumFolder(directory, manifest);
                if (resumeProjects) {
                    if (valid) safeThis->startAlbumProjects(std::move(manifest), directory);
                    else safeThis->showActivity(safeThis->uiText(
                        "Retomada indisponível. Escolha uma pasta com um manifesto de álbum válido.",
                        "Resume unavailable. Choose a folder with a valid album manifest.",
                        "Reprise indisponible. Choisissez un dossier avec un manifeste d’album valide.",
                        "Reanudación no disponible. Elija una carpeta con un manifiesto de álbum válido."));
                    return;
                }
                for (int index = 0; valid && index < manifest.count; ++index) {
                    const auto& track = manifest.tracks[static_cast<size_t>(index)];
                    valid = !track.projectFile.empty() &&
                        (track.state == risco::AlbumTrackState::projectReady ||
                         track.state == risco::AlbumTrackState::rendered);
                }
                if (!valid) {
                    safeThis->showActivity(safeThis->uiText("Render não iniciado · manifesto inválido ou projetos ausentes", "Render not started · invalid manifest or missing projects", "Rendu non démarré · manifeste invalide ou projets manquants", "Render no iniciado · manifiesto no válido o faltan proyectos"));
                    safeThis->status.setText(safeThis->uiText("Manifesto inválido ou há faixas sem projeto; gere/retome os projetos antes do WAV.", "Invalid manifest or tracks without projects; generate or resume the projects before rendering WAVs.", "Manifeste invalide ou morceaux sans projet ; générez ou reprenez les projets avant le rendu WAV.", "Manifiesto no válido o piezas sin proyecto; genere o reanude los proyectos antes de renderizar WAV."),
                                             juce::dontSendNotification);
                    return;
                }
                if (safeThis->albumJob && safeThis->albumJob->isThreadRunning()) return;
                if (directory.getBytesFreeOnVolume() < 5000000000LL) {
                    safeThis->showActivity(safeThis->uiText("Render não iniciado · espaço insuficiente", "Render not started · insufficient free space", "Rendu non démarré · espace libre insuffisant", "Render no iniciado · espacio libre insuficiente"));
                    safeThis->status.setText(safeThis->uiText("Mantenha pelo menos 5 GB livres além do espaço estimado dos renders.", "Keep at least 5 GB free beyond the estimated render size.", "Gardez au moins 5 Go libres en plus de la taille estimée des rendus.", "Mantenga al menos 5 GB libres además del tamaño estimado de los renders."),
                                             juce::dontSendNotification);
                    return;
                }
                safeThis->showActivity(safeThis->uiText("Preparando render · ", "Preparing render · ", "Préparation du rendu · ", "Preparando render · ") + juce::String(manifest.count) + safeThis->uiText(" faixas", " tracks", " morceaux", " piezas"));
                safeThis->lastAlbumDirectory = directory;
                safeThis->status.setText(safeThis->uiText("Pasta do álbum · ", "Album folder · ", "Dossier de l’album · ", "Carpeta del álbum · ") + directory.getFullPathName(),
                                         juce::dontSendNotification);
                safeThis->albumRenderJob = std::make_unique<AlbumRenderJob>(
                    std::move(manifest), directory, safeThis.getComponent(),
                    safeThis->languageSelect.getSelectedId() - 1);
                safeThis->albumRenderJob->completed = [safeThis](juce::String result) {
                    if (safeThis) safeThis->showActivity(result);
                };
                safeThis->albumRenderJob->launchThread(juce::Thread::Priority::low);
            });
    }
    static std::uint64_t freshSeedTrackSeed(std::uint64_t materialSeed, std::uint64_t previous = 0) {
        auto& random = juce::Random::getSystemRandom();
        const auto high = static_cast<std::uint32_t>(random.nextInt());
        const auto low = static_cast<std::uint32_t>(random.nextInt());
        const auto nonce = (static_cast<std::uint64_t>(high) << 32u) | low;
        auto seed = materialSeed == 0 ? nonce :
            risco::formulaSeedFromText(std::to_string(materialSeed) + ":" + std::to_string(nonce));
        if (seed == 0) seed = 1;
        if (seed == previous) seed = (seed == std::numeric_limits<std::uint64_t>::max()) ? 1 : seed + 1;
        return seed;
    }

    void openSeedTrackDialog() {
        seedTrackDialog = std::make_unique<juce::AlertWindow>(
            uiText("Gerar faixa completa", "Generate a full track", "Générer un morceau complet", "Generar pieza completa"),
            uiText("Gera uma faixa editável em cinco canais. Com Texto ou Desenho como origem, a faixa apresenta e retoma um motivo da inscrição. Ouça a proposta antes de salvar.",
                   "Generates an editable track with five channels. With Text or Drawing as the source, the track introduces and reprises a motif from it. Preview the proposal before saving.",
                   "Génère un morceau modifiable à cinq pistes. Avec Texte ou Dessin comme source, le morceau présente et reprend un motif de l’inscription. Écoutez la proposition avant de sauvegarder.",
                   "Genera una pieza editable en cinco canales. Con Texto o Dibujo como origen, la pieza presenta y retoma un motivo de la inscripción. Escucha la propuesta antes de guardar."),
            juce::MessageBoxIconType::NoIcon);
        auto defaultSeed = static_cast<std::uint64_t>(juce::Time::getCurrentTime().toMilliseconds());
        auto seedLabel = uiText("Seed sugerida:", "Suggested seed:", "Seed suggérée :", "Seed sugerida:");
        const auto source = seedSource.getSelectedId();
        auto material = risco::SeedTrackMaterial::none;
        auto cue = risco::SeedTrackCue::none;
        risco::SeedTrackMotif motif;
        if (source == 2 && ascii.getText().isNotEmpty()) {
            const auto input = ascii.getText().toStdString();
            defaultSeed = risco::formulaSeedFromText(input);
            material = risco::SeedTrackMaterial::text;
            cue = risco::seedTrackTextCue(input);
            motif = risco::seedTrackMotif(risco::composeText(input));
            seedLabel = uiText("Origem do texto:", "Text source:", "Source texte :", "Origen del texto:");
        } else if (source == 3 && inscription.count > 0) {
            defaultSeed = risco::formulaSeedFromDrawing(inscription);
            material = risco::SeedTrackMaterial::drawing;
            cue = risco::seedTrackDrawingCue(inscription);
            motif = risco::seedTrackMotif(risco::compose(inscription));
            seedLabel = uiText("Origem do desenho:", "Drawing source:", "Source dessin :", "Origen del dibujo:");
        }
        seedTrackDialog->addTextEditor("seed", juce::String(std::max(std::uint64_t{1}, defaultSeed)), seedLabel);
        if (auto* editor = seedTrackDialog->getTextEditor("seed"))
            editor->setInputRestrictions(20, "0123456789");
        seedTrackDialog->addComboBox("form", {
            uiText("Retorno do tema", "Theme return", "Retour du thème", "Retorno del tema"),
            uiText("Colagem e rupturas", "Collage and breaks", "Collage et ruptures", "Collage y rupturas")},
            uiText("Forma:", "Form:", "Forme :", "Forma:"));
        seedTrackDialog->getComboBoxComponent("form")->setSelectedId(1, juce::dontSendNotification);
        seedTrackDialog->addComboBox("arrangement", {
            uiText("Por partes", "By parts", "Par sections", "Por secciones"),
            uiText("Contínuo", "Continuous", "Continu", "Continuo")},
            uiText("Arranjo:", "Arrangement:", "Arrangement :", "Arreglo:"));
        seedTrackDialog->getComboBoxComponent("arrangement")->setSelectedId(1, juce::dontSendNotification);
        seedTrackDialog->addComboBox("surprise", {
            uiText("Contida", "Restrained", "Contenue", "Contenida"),
            uiText("Média", "Medium", "Moyenne", "Media"),
            uiText("Alta", "High", "Élevée", "Alta")},
            uiText("Surpresa:", "Surprise:", "Surprise :", "Sorpresa:"));
        seedTrackDialog->getComboBoxComponent("surprise")->setSelectedId(2, juce::dontSendNotification);
        seedTrackDialog->addComboBox("motifReturn", {
            uiText("Automático · pela surpresa", "Automatic · from surprise", "Automatique · selon la surprise", "Automático · según la sorpresa"),
            uiText("Literal", "Literal", "Littéral", "Literal"),
            uiText("Espelho", "Mirror", "Miroir", "Espejo"),
            uiText("Espelho + retrógrado", "Mirror + retrograde", "Miroir + rétrograde", "Espejo + retrógrado")},
            uiText("Retorno do motivo:", "Motif return:", "Retour du motif :", "Retorno del motivo:"));
        seedTrackDialog->getComboBoxComponent("motifReturn")->setSelectedId(1, juce::dontSendNotification);
        seedTrackDialog->getComboBoxComponent("motifReturn")->setEnabled(motif.count > 0);
        seedTrackDialog->addComboBox("duration", {
            uiText("Breve · 32", "Short · 32", "Court · 32", "Breve · 32"),
            uiText("Média · 64", "Medium · 64", "Moyen · 64", "Media · 64"),
            uiText("Longa · 80", "Long · 80", "Long · 80", "Larga · 80")},
            uiText("Duração:", "Length:", "Durée :", "Duración:"));
        seedTrackDialog->getComboBoxComponent("duration")->setSelectedId(2, juce::dontSendNotification);
        seedTrackDialog->addComboBox("pulse", {
            uiText("Reto", "Straight", "Droit", "Recto"),
            uiText("Balanço", "Swing", "Swing", "Swing"),
            uiText("Cortado", "Cut", "Coupé", "Cortado")},
            uiText("Pulso:", "Pulse:", "Pulsation :", "Pulso:"));
        seedTrackDialog->getComboBoxComponent("pulse")->setSelectedId(1, juce::dontSendNotification);
        seedTrackDialog->addComboBox("palette", {
            uiText("Original", "Original", "Original", "Original"),
            uiText("Ressonante", "Resonant", "Résonante", "Resonante"),
            uiText("Chip", "Chip", "Chip", "Chip"),
            uiText("Grão", "Grain", "Grain", "Grano")},
            uiText("Paleta:", "Palette:", "Palette :", "Paleta:"));
        seedTrackDialog->getComboBoxComponent("palette")->setSelectedId(1, juce::dontSendNotification);
        seedTrackDialog->addButton(uiText("Gerar proposta", "Generate proposal", "Générer une proposition", "Generar propuesta"), 1,
            juce::KeyPress(juce::KeyPress::returnKey));
        seedTrackDialog->addButton(uiText("Nova seed", "New seed", "Nouvelle seed", "Nueva seed"), 2);
        seedTrackDialog->addButton(uiText("Cancelar", "Cancel", "Annuler", "Cancelar"), 0,
            juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        seedTrackDialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, sourceSeed = defaultSeed, material, cue, motif](int choice) {
                if (!safeThis || !safeThis->seedTrackDialog) return;
                const auto text = safeThis->seedTrackDialog->getTextEditorContents("seed").trim().toStdString();
                const auto* formChoice = safeThis->seedTrackDialog->getComboBoxComponent("form");
                const auto* arrangementChoice = safeThis->seedTrackDialog->getComboBoxComponent("arrangement");
                const auto* surpriseChoice = safeThis->seedTrackDialog->getComboBoxComponent("surprise");
                const auto* motifReturnChoice = safeThis->seedTrackDialog->getComboBoxComponent("motifReturn");
                const auto* durationChoice = safeThis->seedTrackDialog->getComboBoxComponent("duration");
                const auto* pulseChoice = safeThis->seedTrackDialog->getComboBoxComponent("pulse");
                const auto* paletteChoice = safeThis->seedTrackDialog->getComboBoxComponent("palette");
                risco::SeedTrackOptions options;
                options.generatorVersion = 2;
                options.form = formChoice->getSelectedId() == 2
                    ? risco::SeedTrackForm::collage : risco::SeedTrackForm::arc;
                options.arrangeByParts = arrangementChoice->getSelectedId() == 1;
                options.material = material;
                options.materialSeed = material == risco::SeedTrackMaterial::none ? 0 : sourceSeed;
                options.cue = options.arrangeByParts ? cue : risco::SeedTrackCue::none;
                options.motif = motif;
                if (motif.count > 0) options.motifStem = material == risco::SeedTrackMaterial::text ? 0 : 4;
                options.surprise = surpriseChoice->getSelectedId() - 1;
                options.motifReturn = static_cast<risco::SeedTrackMotifReturn>(
                    std::clamp(motifReturnChoice->getSelectedId() - 1, 0, 3));
                constexpr int durations[] = {32, 64, 80};
                options.durationBars = durations[std::clamp(durationChoice->getSelectedId() - 1, 0, 2)];
                options.pulse = static_cast<risco::SeedTrackPulse>(
                    std::clamp(pulseChoice->getSelectedId() - 1, 0, 2));
                options.palette = static_cast<risco::SeedTrackPalette>(
                    std::clamp(paletteChoice->getSelectedId() - 1, 0, 3));
                const auto choices = formChoice->getText() + utf8(" · ") + arrangementChoice->getText() +
                    safeThis->uiText(" · surpresa ", " · surprise ", " · surprise ", " · sorpresa ") +
                    surpriseChoice->getText().toLowerCase() +
                    safeThis->uiText(" · retorno ", " · return ", " · retour ", " · retorno ") +
                    motifReturnChoice->getText().toLowerCase() + utf8(" · ") +
                    pulseChoice->getText() + utf8(" · ") + paletteChoice->getText();
                safeThis->seedTrackDialog.reset();
                if (choice != 1 && choice != 2) return;
                std::uint64_t seed = 0;
                if (choice == 2) {
                    seed = freshSeedTrackSeed(options.materialSeed);
                } else {
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), seed);
                    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || seed == 0) {
                        safeThis->status.setText(safeThis->uiText("Seed inválida; nenhum projeto foi alterado.", "Invalid seed; no project was changed.", "Seed invalide ; aucun projet modifié.", "Seed no válida; no se cambió ningún proyecto."), juce::dontSendNotification);
                        return;
                    }
                }
                safeThis->showSeedTrackProposal(seed, choices, options, {}, {});
            }), false);
    }
    struct SeedTrackProposal {
        std::shared_ptr<risco::Song> song;
        risco::SeedTrackOptions options;
    };

    juce::StringArray seedTrackChannelLabels() {
        return {
            uiText("melodia", "melody", "mélodie", "melodía"),
            uiText("harmonia", "harmony", "harmonie", "armonía"),
            uiText("baixo", "bass", "basse", "bajo"),
            uiText("ritmo", "rhythm", "rythme", "ritmo"),
            uiText("textura", "texture", "texture", "textura")};
    }

    juce::StringArray seedTrackPartLabels() {
        return {
            uiText("Abertura", "Opening", "Ouverture", "Apertura"),
            uiText("Desenvolvimento", "Development", "Développement", "Desarrollo"),
            uiText("Quebra", "Break", "Rupture", "Corte"),
            uiText("Retorno", "Return", "Retour", "Retorno"),
            uiText("Final", "Ending", "Finale", "Final")};
    }

    juce::StringArray seedTrackPartDynamicsLabels() {
        return {
            uiText("Automática", "Automatic", "Automatique", "Automática"),
            uiText("Estável", "Steady", "Stable", "Estable"),
            uiText("Crescendo", "Crescendo", "Crescendo", "Crescendo"),
            uiText("Diminuendo", "Diminuendo", "Diminuendo", "Diminuendo"),
            uiText("Arco", "Arch", "Arche", "Arco")};
    }

    class SeedTrackPartControls final : public juce::Component {
    public:
        SeedTrackPartControls(const juce::StringArray& labels, const juce::String& repeatLabel) {
            for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
                auto& toggle = channels[static_cast<std::size_t>(stem)];
                toggle.setButtonText(labels[stem]);
                addAndMakeVisible(toggle);
            }
            repeat.setButtonText(repeatLabel);
            addAndMakeVisible(repeat);
            setSize(510, 62);
        }
        void setPart(const risco::SeedTrackPart& part) {
            for (int stem = 0; stem < risco::Song::stemCount; ++stem)
                channels[static_cast<std::size_t>(stem)].setToggleState(
                    (part.channels & (1u << stem)) != 0, juce::dontSendNotification);
            repeat.setToggleState(part.repeatFirst, juce::dontSendNotification);
        }
        unsigned mask() const {
            unsigned selected = 0;
            for (int stem = 0; stem < risco::Song::stemCount; ++stem)
                if (channels[static_cast<std::size_t>(stem)].getToggleState()) selected |= 1u << stem;
            return selected;
        }
        bool repeats() const { return repeat.getToggleState(); }
    private:
        void resized() override {
            auto bounds = getLocalBounds();
            auto row = bounds.removeFromTop(bounds.getHeight() / 2);
            const int width = row.getWidth() / risco::Song::stemCount;
            for (auto& toggle : channels) toggle.setBounds(row.removeFromLeft(width));
            repeat.setBounds(bounds);
        }
        std::array<juce::ToggleButton, risco::Song::stemCount> channels;
        juce::ToggleButton repeat;
    };

    void openSeedTrackPartEditor(const juce::String& choices, SeedTrackProposal take,
                                 SeedTrackProposal alternate, int initialPart,
                                 const juce::String& notice = {}) {
        if (!take.song) return;
        const auto seed = take.song->compositionSeed;
        const auto parts = risco::seedTrackParts(seed, take.options);
        seedTrackPartDialog = std::make_unique<juce::AlertWindow>(
            uiText("Editar uma parte", "Edit a part", "Modifier une section", "Editar una sección"),
            (notice.isEmpty() ? juce::String() : notice + "\n") +
            uiText("Escolha a parte, marque os canais e ajuste a duração em múltiplos de 4 compassos.",
                   "Choose a part, select its channels and set the length in multiples of 4 bars.",
                   "Choisissez une section, cochez ses pistes et réglez la durée par multiples de 4 mesures.",
                   "Elija una sección, marque los canales y ajuste la duración en múltiplos de 4 compases."),
            juce::MessageBoxIconType::NoIcon, this);
        auto& dialog = *seedTrackPartDialog;
        dialog.addComboBox("part", seedTrackPartLabels(), uiText("Parte:", "Part:", "Section :", "Sección:"));
        dialog.addTextEditor("bars", {}, uiText("Compassos:", "Bars:", "Mesures :", "Compases:"));
        dialog.addComboBox("gain", seedTrackPartDynamicsLabels(), uiText("Dinâmica:", "Dynamics:", "Dynamique :", "Dinámica:"));
        auto* partChoice = dialog.getComboBoxComponent("part");
        auto* barsEditor = dialog.getTextEditor("bars");
        auto* gainChoice = dialog.getComboBoxComponent("gain");
        barsEditor->setInputRestrictions(2, "0123456789");
        auto controls = std::make_shared<SeedTrackPartControls>(seedTrackChannelLabels(),
            uiText("Repetir o primeiro bloco de quatro compassos", "Repeat the first four-bar block",
                   "Répéter le premier bloc de quatre mesures", "Repetir el primer bloque de cuatro compases"));
        dialog.addCustomComponent(controls.get());
        for (std::size_t i = 0; i < parts.size(); ++i)
            partChoice->setItemEnabled(static_cast<int>(i) + 1, parts[i].endBlock > parts[i].firstBlock);
        partChoice->setSelectedId(initialPart, juce::dontSendNotification);
        partChoice->onChange = [partChoice, barsEditor, gainChoice, controls, parts, edits = take.options.partEdits] {
            const auto index = static_cast<std::size_t>(std::max(1, partChoice->getSelectedId()) - 1);
            const auto& part = parts[index];
            barsEditor->setText(juce::String((part.endBlock - part.firstBlock) * 4), false);
            gainChoice->setSelectedId(static_cast<int>(edits[index].dynamics) + 1, juce::dontSendNotification);
            controls->setPart(part);
        };
        partChoice->onChange();
        dialog.addButton(uiText("Aplicar", "Apply", "Appliquer", "Aplicar"), 1,
                         juce::KeyPress(juce::KeyPress::returnKey));
        dialog.addButton(uiText("Restaurar automático", "Restore automatic", "Rétablir l’automatique", "Restaurar automático"), 2);
        dialog.addButton(uiText("Voltar", "Back", "Retour", "Volver"), 0,
                         juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        dialog.enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, choices, seed, parts, controls, take = std::move(take),
             alternate = std::move(alternate)](int choice) {
                if (!safeThis || !safeThis->seedTrackPartDialog) return;
                auto& dialog = *safeThis->seedTrackPartDialog;
                const int id = dialog.getComboBoxComponent("part")->getSelectedId();
                const auto bars = dialog.getTextEditorContents("bars").trim().getIntValue();
                const int gainId = dialog.getComboBoxComponent("gain")->getSelectedId();
                safeThis->seedTrackPartDialog.reset();
                if (choice == 0 || id < 1 || id > int(parts.size())) {
                    safeThis->reviewSeedTrack(take, choices, std::move(alternate));
                    return;
                }

                const auto index = static_cast<std::size_t>(id - 1);
                const auto& part = parts[index];
                auto revised = take.options;
                auto& edit = revised.partEdits[index];
                if (choice == 2) {
                    edit = {};
                } else if (bars < 4 || bars > risco::seedTrackMaxBars || bars % 4 != 0 || controls->mask() == 0) {
                    safeThis->openSeedTrackPartEditor(choices, take, std::move(alternate), id,
                        safeThis->uiText("Marque pelo menos um canal e use um múltiplo de 4 compassos.",
                            "Select at least one channel and use a multiple of 4 bars.",
                            "Cochez au moins une piste et utilisez un multiple de 4 mesures.",
                            "Marque al menos un canal y use un múltiplo de 4 compases."));
                    return;
                } else {
                    if (bars != (part.endBlock - part.firstBlock) * 4) edit.blocks = bars / 4;
                    if (controls->mask() != part.channels) edit.channels = controls->mask();
                    edit.dynamics = static_cast<risco::SeedTrackPartDynamics>(std::clamp(gainId - 1, 0, 4));
                    edit.repeatFirst = controls->repeats();
                }
                const int totalBars = risco::seedTrackParts(seed, revised).back().endBlock * 4;
                if (totalBars < risco::seedTrackMinBars || totalBars > risco::seedTrackMaxBars) {
                    safeThis->openSeedTrackPartEditor(choices, take, std::move(alternate), id,
                        safeThis->uiText("A faixa precisa ter entre 16 e 84 compassos.",
                            "The track must have between 16 and 84 bars.",
                            "Le morceau doit comporter entre 16 et 84 mesures.",
                            "La pieza debe tener entre 16 y 84 compases."));
                    return;
                }
                const auto& before = take.options.partEdits[index];
                if (edit.blocks == before.blocks && edit.channels == before.channels &&
                    edit.dynamics == before.dynamics && edit.repeatFirst == before.repeatFirst) {
                    safeThis->reviewSeedTrack(take, choices, std::move(alternate));
                    return;
                }
                safeThis->showSeedTrackProposal(seed, choices, revised, take, std::move(alternate));
            }), false);
    }

    void showSeedTrackProposal(std::uint64_t seed, const juce::String& choices,
                               const risco::SeedTrackOptions& options,
                               SeedTrackProposal previous, SeedTrackProposal previousAlternate) {
        auto generationOptions = options;
        generationOptions.motifReturnStem = risco::alternateMotifStem(seed, options.motifStem);
        auto candidate = std::make_shared<risco::Song>();
        if (!risco::generateSeedTrack(seed, generationOptions, *candidate)) {
            const auto error = uiText("Não foi possível gerar a faixa.", "Could not generate the track.",
                                      "Impossible de générer le morceau.", "No se pudo generar la pieza.");
            if (previous.song)
                reviewSeedTrack(std::move(previous), choices, std::move(previousAlternate), error);
            status.setText(error, juce::dontSendNotification);
            return;
        }
        reviewSeedTrack({std::move(candidate), generationOptions}, choices, std::move(previous));
    }

    void reviewSeedTrack(SeedTrackProposal take, const juce::String& choices,
                         SeedTrackProposal alternate, const juce::String& notice = {}) {
        const auto candidate = take.song;
        const auto& options = take.options;
        if (!candidate) return;
        showActivity(utf8("Faixa gerada · seed ") + juce::String(candidate->compositionSeed) +
                     " · " + choices);
        juce::String arrangement;
        const auto channels = seedTrackChannelLabels();
        const auto parts = risco::seedTrackParts(candidate->compositionSeed, options);
        const int durationBars = options.arrangeByParts ? parts.back().endBlock * 4 : options.durationBars;
        if (options.motifStem >= 0) {
            const auto& returnPart = parts[static_cast<std::size_t>(risco::SeedTrackPartName::returnSection)];
            const auto motifVoice = [&channels](int stem, unsigned active) -> juce::String {
                return stem >= 0 && (active & (1u << stem)) != 0 ? channels[stem] : utf8("—");
            };
            arrangement += uiText("Motivo · ", "Motif · ", "Motif · ", "Motivo · ") +
                motifVoice(options.motifStem, parts.front().channels) + utf8(" → ") +
                motifVoice(options.motifReturnStem,
                    options.arrangeByParts ? returnPart.channels : parts.front().channels) + "\n";
            const auto returnTransform = options.motifReturn != risco::SeedTrackMotifReturn::automatic
                ? options.motifReturn
                : options.surprise <= 0 ? risco::SeedTrackMotifReturn::literal
                : options.surprise == 1 ? risco::SeedTrackMotifReturn::mirror
                                        : risco::SeedTrackMotifReturn::mirrorRetrograde;
            if (options.generatorVersion >= 2 && returnTransform != risco::SeedTrackMotifReturn::literal)
                arrangement += returnTransform == risco::SeedTrackMotifReturn::mirrorRetrograde
                    ? uiText("Retorno do motivo · espelho e ordem retrógrada\n",
                             "Motif return · inverted and retrograde\n",
                             "Retour du motif · inversé et rétrograde\n",
                             "Retorno del motivo · invertido y retrógrado\n")
                    : uiText("Retorno do motivo · espelho do contorno\n",
                             "Motif return · mirrored contour\n",
                             "Retour du motif · contour inversé\n",
                             "Retorno del motivo · contorno invertido\n");
        }
        if (options.arrangeByParts) {
            const auto names = seedTrackPartLabels();
            const auto dynamics = seedTrackPartDynamicsLabels();
            for (const auto& part : parts) {
                if (part.endBlock <= part.firstBlock) continue;
                juce::StringArray active;
                for (int stem = 0; stem < risco::Song::stemCount; ++stem)
                    if ((part.channels & (1u << stem)) != 0) active.add(channels[stem]);
                arrangement += names[static_cast<int>(part.name)] + " " +
                    juce::String(part.firstBlock * 4 + 1) + utf8("–") + juce::String(part.endBlock * 4) +
                    utf8(" · ") + active.joinIntoString(", ");
                if (part.repeatFirst)
                    arrangement += uiText(" · repetição", " · repeat", " · répétition", " · repetición");
                const auto gain = options.partEdits[static_cast<std::size_t>(part.name)].dynamics;
                if (gain != risco::SeedTrackPartDynamics::automatic)
                    arrangement += utf8(" · ") + dynamics[static_cast<int>(gain)].toLowerCase();
                arrangement += "\n";
            }
            switch (options.cue) {
                case risco::SeedTrackCue::none: break;
                case risco::SeedTrackCue::breath:
                    arrangement += uiText("Pontuação do texto · pausa antes do retorno\n", "Text punctuation · pause before the return\n",
                        "Ponctuation du texte · pause avant le retour\n", "Puntuación del texto · pausa antes del retorno\n");
                    break;
                case risco::SeedTrackCue::rise:
                    if (options.partEdits[1].dynamics == risco::SeedTrackPartDynamics::automatic)
                        arrangement += uiText("Traço ascendente · crescendo no desenvolvimento\n", "Rising stroke · crescendo in development\n",
                        "Trait ascendant · crescendo dans le développement\n", "Trazo ascendente · crescendo en el desarrollo\n");
                    break;
                case risco::SeedTrackCue::fall:
                    if (options.partEdits[1].dynamics == risco::SeedTrackPartDynamics::automatic)
                        arrangement += uiText("Traço descendente · diminuendo no desenvolvimento\n", "Falling stroke · diminuendo in development\n",
                        "Trait descendant · diminuendo dans le développement\n", "Trazo descendente · diminuendo en el desarrollo\n");
                    break;
            }
        }
        juce::String comparison;
        if (alternate.song) {
            if (alternate.song->compositionSeed == candidate->compositionSeed)
                comparison = uiText("A/B · outro arranjo da mesma seed\n", "A/B · another arrangement of this seed\n",
                                    "A/B · autre arrangement de cette graine\n", "A/B · otro arreglo de esta semilla\n");
            else
                comparison = uiText("Seed de comparação: ", "Comparison seed: ", "Seed de comparaison : ",
                                    "Seed de comparación: ") + juce::String(alternate.song->compositionSeed) + "\n";
        }
        seedTrackReviewDialog = std::make_unique<juce::AlertWindow>(
            uiText("Revisar faixa gerada", "Review generated track", "Réviser le morceau généré", "Revisar pieza generada"),
            (notice.isEmpty() ? juce::String() : notice + "\n") +
            utf8("Seed: ") + juce::String(candidate->compositionSeed) + "\n" +
            comparison +
            choices + "\n" +
            uiText("Duração estrutural: ", "Structural length: ", "Durée structurelle : ", "Duración estructural: ") +
            juce::String(durationBars) + uiText(" compassos · ", " bars · ", " mesures · ", " compases · ") +
            juce::String(candidate->bpm, 1) + " BPM\n" +
            uiText("Células: ", "Cells: ", "Cellules : ", "Células: ") + juce::String(candidate->cellCount) +
            "\n" + arrangement +
            uiText("Ouça a proposta antes de salvar. Ao salvar, a faixa abre e toca uma vez.", "Preview before saving. Saving opens the new track and plays it once.", "Écoutez avant de sauvegarder. La sauvegarde ouvre le morceau et le joue une fois.", "Escucha antes de guardar. Al guardar, la pieza se abre y suena una vez."),
            juce::MessageBoxIconType::QuestionIcon, this);
        seedTrackReviewDialog->addButton(uiText("Ouvir prévia", "Play preview", "Écouter l’aperçu", "Oír vista previa"), 0);
        seedTrackReviewDialog->getButton(0)->onClick = [this, candidate] {
            if (seedTrackPreviewPlaying.load(std::memory_order_relaxed)) {
                const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
                seedTrackPreview->frame = seedTrackPreview->endFrame;
                seedTrackPreviewPlaying.store(false, std::memory_order_relaxed);
                return;
            }
            auto* device = deviceManager.getCurrentAudioDevice();
            const double rate = device != nullptr && device->isPlaying() ? device->getCurrentSampleRate() : 0.0;
            if (!std::isfinite(rate) || rate <= 0.0) {
                seedTrackReviewDialog->setMessage(utf8("Prévia indisponível. Nenhuma saída de áudio ativa."));
                return;
            }
            auto playback = std::unique_ptr<risco::SongPlayback>(
                new risco::SongPlayback(risco::prepareSongPlayback(*candidate)));
            if (playback->count == 0 || !risco::SongRender::validDuration(playback->end)) {
                seedTrackReviewDialog->setMessage(utf8("Não foi possível preparar a prévia desta faixa."));
                return;
            }
            auto prepared = std::make_unique<SeedTrackPreview>(candidate, std::move(playback), rate);
            {
                // JUCE already holds this lock while dispatching the audio callback.
                const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
                seedTrackPreview.swap(prepared);
                seedTrackPreviewPlaying.store(true, std::memory_order_relaxed);
            }
        };
        seedTrackReviewDialog->addButton(uiText("Salvar e tocar uma vez…", "Save and play once…", "Sauver et jouer une fois…", "Guardar y tocar una vez…"), 1,
            juce::KeyPress(juce::KeyPress::returnKey));
        seedTrackReviewDialog->addButton(uiText("Nova seed", "New seed", "Nouvelle seed", "Nueva seed"), 2);
        if (alternate.song)
            seedTrackReviewDialog->addButton(uiText("Trocar A/B", "Swap A/B", "Basculer A/B", "Cambiar A/B"), 3);
        if (options.arrangeByParts)
            seedTrackReviewDialog->addButton(uiText("Editar partes…", "Edit parts…", "Modifier les sections…", "Editar secciones…"), 4);
        seedTrackReviewDialog->addButton(uiText("Cancelar", "Cancel", "Annuler", "Cancelar"), 0,
            juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        seedTrackReviewDialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, take = std::move(take), choices,
             alternate = std::move(alternate)](int choice) {
                if (!safeThis) return;
                std::unique_ptr<SeedTrackPreview> finished;
                {
                    const juce::ScopedLock lock(safeThis->deviceManager.getAudioCallbackLock());
                    finished.swap(safeThis->seedTrackPreview);
                    safeThis->seedTrackPreviewPlaying.store(false, std::memory_order_relaxed);
                }
                safeThis->seedTrackReviewDialog.reset();
                finished.reset();
                const auto candidate = take.song;
                if (choice == 2) {
                    const auto seed = freshSeedTrackSeed(take.options.materialSeed, candidate->compositionSeed);
                    safeThis->showSeedTrackProposal(seed, choices, take.options, take, std::move(alternate));
                    return;
                }
                if (choice == 3 && alternate.song) {
                    safeThis->reviewSeedTrack(std::move(alternate), choices, take);
                    return;
                }
                if (choice == 4) {
                    safeThis->openSeedTrackPartEditor(choices, take, std::move(alternate), 1);
                    return;
                }
                if (choice != 1) {
                    safeThis->showActivity(utf8("Proposta de faixa descartada"));
                    return;
                }
                const auto initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                    .getChildFile("risco-seed-" + juce::String(candidate->compositionSeed) + ".risco");
                safeThis->fileChooser = std::make_unique<juce::FileChooser>(
                    utf8("Salvar nova faixa Risco"), initial, "*.risco");
                safeThis->fileChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                    juce::FileBrowserComponent::canSelectFiles |
                    juce::FileBrowserComponent::warnAboutOverwriting,
                    [safeThis, candidate](const juce::FileChooser& chooser) {
                        if (!safeThis) return;
                        if (chooser.getResult() == juce::File()) {
                            safeThis->showActivity(utf8("Gravação da faixa cancelada"));
                            return;
                        }
                        std::ostringstream output;
                        if (!risco::saveSong(output, *candidate)) {
                            safeThis->status.setText(utf8("A faixa gerada não passou na validação de projeto; nada foi gravado."),
                                                     juce::dontSendNotification);
                            return;
                        }
                        const auto bytes = output.str();
                        const auto file = chooser.getResult().withFileExtension(".risco");
                        if (bytes.size() > 4 * 1024 * 1024 ||
                            !file.replaceWithData(bytes.data(), bytes.size())) {
                            safeThis->status.setText(utf8("Falha ao gravar o novo projeto; a composição aberta foi preservada."),
                                                     juce::dontSendNotification);
                            return;
                        }
                        if (safeThis->loadFromFile(file)) {
                            if (!safeThis->playArrangement()) return;
                            safeThis->showActivity(utf8("Faixa salva · ") + file.getFileName());
                        }
                    });
            }), false);
    }
    void reviewCompositionSeedCandidate(std::shared_ptr<risco::Song> candidate) {
        if (!candidate) return;
        const auto& before = song;
        int changedPhrases = 0, changedRoles = 0, changedMeters = 0, changedStemMix = 0, changedFx = 0;
        for (int i = 0; i < before.cellCount && i < candidate->cellCount; ++i)
            if (before.bank[static_cast<size_t>(i)].phraseSeed != candidate->bank[static_cast<size_t>(i)].phraseSeed ||
                before.bank[static_cast<size_t>(i)].phraseVariation != candidate->bank[static_cast<size_t>(i)].phraseVariation)
                ++changedPhrases;
        for (int i = 0; i < risco::Song::stemCount; ++i) {
            const auto index = static_cast<size_t>(i);
            changedRoles += before.stemRole[index] != candidate->stemRole[index];
            changedMeters += before.stemMeter[index] != candidate->stemMeter[index];
            changedStemMix += before.stemGain[index] != candidate->stemGain[index] ||
                              before.stemPan[index] != candidate->stemPan[index];
            changedFx += before.stemEffect[index] != candidate->stemEffect[index] ||
                         before.stemFilterCutoff[index] != candidate->stemFilterCutoff[index] ||
                         before.stemBitcrushBits[index] != candidate->stemBitcrushBits[index] ||
                         before.stemDelayMix[index] != candidate->stemDelayMix[index] ||
                         before.stemReverbMix[index] != candidate->stemReverbMix[index];
        }
        juce::String summary = utf8("Seed: ") + juce::String(candidate->compositionSeed) + "\n" +
            utf8("Andamento: ") + juce::String(before.bpm, 1) + " → " + juce::String(candidate->bpm, 1) + " BPM\n" +
            utf8("Células com frase alterada: ") + juce::String(changedPhrases) + "\n" +
            utf8("Papéis e métricas de stem alterados: ") + juce::String(changedRoles) + " / " + juce::String(changedMeters) + "\n" +
            utf8("Stems com FX alterados: ") + juce::String(changedFx) + " · mixagens alteradas: " + juce::String(changedStemMix) + "\n" +
            utf8("MASTER permanece protegido pelas travas escolhidas. Aplicar salva a música atual como versão recuperável.");
        showActivity(utf8("Proposta por seed ") + juce::String(candidate->compositionSeed) +
                     " · " + juce::String(changedPhrases) + utf8(" frases · ") +
                     juce::String(changedFx) + " FX · " + juce::String(changedStemMix) + utf8(" mixagens"));
        compositionSeedPreviewDialog = std::make_unique<juce::AlertWindow>(
            utf8("Revisar proposta generativa"), summary, juce::MessageBoxIconType::QuestionIcon);
        compositionSeedPreviewDialog->addButton(uiText("Aplicar variação", "Apply variation", "Appliquer la variation", "Aplicar variación"), 1,
            juce::KeyPress(juce::KeyPress::returnKey));
        compositionSeedPreviewDialog->addButton(uiText("Cancelar", "Cancel", "Annuler", "Cancelar"), 0,
            juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        compositionSeedPreviewDialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, candidate = std::move(candidate)](int choice) {
                if (!safeThis) return;
                safeThis->compositionSeedPreviewDialog.reset();
                if (choice != 1 || !candidate) return;
                if (safeThis->compositionVersions.size() >= 16) {
                    safeThis->status.setText(utf8("Árvore de versões cheia; a proposta foi cancelada sem alterar a música."),
                                             juce::dontSendNotification);
                    return;
                }
                safeThis->saveCompositionVersion();
                safeThis->song = *candidate;
                safeThis->selectedPlacement = -1;
                safeThis->refreshMeterChoices();
                safeThis->refreshTimbreChoices();
                safeThis->refreshBank(0, false);
                safeThis->clearGridUndoHistory();
                safeThis->refreshPlaybackAfterGridEdit();
                safeThis->status.setText(utf8("Variação aplicada; a versão anterior está na árvore. Salve o projeto .risco para persistir a nova receita."),
                                         juce::dontSendNotification);
                safeThis->repaint();
            }), false);
    }
    std::string compositionVersionSignature(const risco::Song& value) const {
        std::ostringstream stream;
        if (!risco::saveSong(stream, value)) return {};
        return stream.str();
    }
    juce::String uniqueCompositionVersionName(const juce::String& prefix, const juce::String& suffix = {}) const {
        for (int index = 1; index <= 16; ++index) {
            const auto candidate = prefix + juce::String(index) + suffix;
            const bool exists = std::any_of(compositionVersions.begin(),compositionVersions.end(),[&](const CompositionVersion& version) {
                return version.name.compareIgnoreCase(candidate) == 0;
            });
            if (!exists) return candidate;
        }
        return prefix + juce::String(compositionVersions.size() + 1) + suffix;
    }
    void saveCompositionVersion() {
        if (compositionVersions.size() >= 16) {
            status.setText(utf8("Limite de 16 versões da sessão atingido; remova ou salve o projeto."), juce::dontSendNotification);
            return;
        }
        CompositionVersion version;
        version.name = uniqueCompositionVersionName(utf8("Versão "),utf8(" · ") +
            juce::String(song.placementCount) + utf8(" células · ") + juce::String(song.cellCount) + utf8(" no banco"));
        version.song = song;
        version.parentIndex = activeCompositionVersion;
        compositionVersions.push_back(std::move(version));
        activeCompositionVersion = int(compositionVersions.size()) - 1;
        status.setText(utf8("Versão guardada: ") + compositionVersions.back().name +
            utf8(" · clique em Salvar projeto para gravá-la no arquivo .risco."), juce::dontSendNotification);
    }
    void restoreCompositionVersion(int index) {
        if (index < 0 || index >= int(compositionVersions.size())) return;
        if (compositionVersionSignature(song) != compositionVersionSignature(compositionVersions[static_cast<size_t>(index)].song)) {
            if (compositionVersions.size() >= 16) {
                status.setText(utf8("A lista tem 16 versões. Remova uma antes de retomar para guardar o estado atual."),juce::dontSendNotification);
                return;
            }
            CompositionVersion current;
            current.name = uniqueCompositionVersionName(utf8("Antes de retomar · "));
            current.song = song;
            current.parentIndex = activeCompositionVersion;
            compositionVersions.push_back(std::move(current));
        }
        song = compositionVersions[static_cast<size_t>(index)].song;
        activeCompositionVersion = index;
        selectedPlacement = -1;
        refreshMeterChoices();
        refreshTimbreChoices();
        refreshBank(0, false);
        clearGridUndoHistory();
        refreshPlaybackAfterGridEdit();
        status.setText(utf8("Versão retomada: ") + compositionVersions[static_cast<size_t>(index)].name +
            utf8(" · a versão anterior foi preservada quando necessário."), juce::dontSendNotification);
        repaint();
    }
    void deleteCompositionVersion(int index) {
        if (index < 0 || index >= int(compositionVersions.size())) return;
        const auto removedName = compositionVersions[static_cast<size_t>(index)].name;
        const int removedParent = compositionVersions[static_cast<size_t>(index)].parentIndex;
        for (int i = 0; i < int(compositionVersions.size()); ++i) {
            if (i == index) continue;
            auto& version = compositionVersions[static_cast<size_t>(i)];
            if (version.parentIndex == index) version.parentIndex = removedParent;
            else if (version.parentIndex > index) --version.parentIndex;
        }
        compositionVersions.erase(compositionVersions.begin() + index);
        if (activeCompositionVersion == index) activeCompositionVersion = -1;
        else if (activeCompositionVersion > index) --activeCompositionVersion;
        status.setText(utf8("Versão removida da lista: ") + removedName +
            utf8(" · o arquivo só muda ao salvar o projeto."),juce::dontSendNotification);
    }
    void compareCompositionVersion() {
        if (activeCompositionVersion < 0 || activeCompositionVersion >= int(compositionVersions.size())) return;
        const auto current = compositionVersionSignature(song);
        const auto saved = compositionVersionSignature(compositionVersions[static_cast<size_t>(activeCompositionVersion)].song);
        const bool same = !current.empty() && current == saved;
        status.setText(same ? utf8("Comparação: a versão ativa e a composição atual são idênticas.") :
            utf8("Comparação: a composição atual difere da versão ativa; retomar não apaga a versão guardada."),
            juce::dontSendNotification);
    }
    void refreshPlaybackAfterGridEdit() {
        if (activeSong.load(std::memory_order_acquire) == nullptr ||
            !playing.load(std::memory_order_acquire)) return;
        if (previewingCell || song.placementCount == 0 || songSnapshots.size() >= 64) {
            playing.store(false, std::memory_order_release);
            activeSong.store(nullptr, std::memory_order_release);
            previewingCell = false;
            return;
        }
        playArrangement(false);
    }
    GridState captureGridState() const {
        GridState state;
        state.placements = song.grid;
        state.count = song.placementCount;
        state.selected = selectedPlacement;
        return state;
    }
    void pushGridUndo(const GridState& state) {
        if (gridUndoCount == int(gridUndoHistory.size())) {
            for (size_t i = 1; i < gridUndoHistory.size(); ++i)
                gridUndoHistory[i - 1] = gridUndoHistory[i];
            gridUndoHistory.back() = state;
        } else {
            gridUndoHistory[static_cast<size_t>(gridUndoCount++)] = state;
        }
        undoGridButton.setEnabled(true);
    }
    void clearGridUndoHistory() {
        bankUndo.clear();
        bankRedo.clear();
        gridUndoCount = 0;
        undoGridButton.setEnabled(false);
    }
    bool undoGridEdit() {
        if (gridUndoCount <= 0) return false;
        const auto state = gridUndoHistory[static_cast<size_t>(--gridUndoCount)];
        song.grid = state.placements;
        song.placementCount = state.count;
        selectedPlacement = state.selected >= 0 && state.selected < song.placementCount
            ? state.selected : -1;
        if (selectedPlacement >= 0) {
            const auto& placement = song.grid[static_cast<size_t>(selectedPlacement)];
            startBeat.setText(juce::String(placement.startBeat));
            pendingStem = placement.stem;
            stemSelect.setSelectedId(pendingStem + 1, juce::dontSendNotification);
        }
        gridScrollBeat = gridViewStart();
        undoGridButton.setEnabled(gridUndoCount > 0);
        refreshPlaybackAfterGridEdit();
        status.setText(utf8("Última edição da grade desfeita; o banco de células foi mantido."),
                       juce::dontSendNotification);
        repaint();
        return true;
    }
    void removeSelectedPlacement() {
        if (song.placementCount <= 0) return;
        const int index = selectedPlacement >= 0 && selectedPlacement < song.placementCount
            ? selectedPlacement : song.placementCount - 1;
        const auto before = captureGridState();
        for (int i = index; i + 1 < song.placementCount; ++i)
            song.grid[static_cast<size_t>(i)] = song.grid[static_cast<size_t>(i + 1)];
        --song.placementCount;
        selectedPlacement = -1;
        pushGridUndo(before);
        refreshPlaybackAfterGridEdit();
        status.setText(utf8("Célula retirada da grade; permanece no banco."), juce::dontSendNotification);
        repaint();
    }
    std::uint64_t routingSeedForCell(const risco::Cell& cell) const noexcept {
        if (cell.phraseSeed != 0) return cell.phraseSeed ^
            (std::uint64_t(cell.phraseVariation) * 0x9e3779b97f4a7c15ULL);
        if (cell.inscription.count > 0) return risco::formulaSeedFromDrawing(cell.inscription);
        if (!cell.inscription.ascii.empty()) return risco::formulaSeedFromText(cell.inscription.ascii);
        if (!cell.formulaExpression.empty()) return risco::formulaSeedFromText(cell.formulaExpression);
        return cell.id;
    }
    risco::CellRole generativeRoleForCell(const risco::Cell& cell,
                                          std::uint64_t seed) const noexcept {
        const auto profile = risco::describeCellAudio(cell, song.bpm, song.beatsPerBar);
        if (profile.restLike) return risco::CellRole::texture;
        if (profile.hasEvents && profile.eventsPerBar >= 5.0f) return risco::CellRole::pulse;
        if (profile.hasEvents && profile.meanMidi < 54.0f) return risco::CellRole::bass;
        if (profile.hasEvents && profile.meanMidi >= 70.0f) return risco::CellRole::melody;
        constexpr risco::CellRole middleRoles[] = {
            risco::CellRole::harmony, risco::CellRole::texture, risco::CellRole::melody};
        return middleRoles[static_cast<std::size_t>(seed % 3u)];
    }
    void placeSelectedCell() {
        const int index = selectedCellIndex();
        const auto value = startBeat.getText();
        if (index < 0 || value.isEmpty() || !value.containsOnly("0123456789")) {
            status.setText(utf8("Escolha uma célula e um tempo inicial válido."),
                           juce::dontSendNotification);
            return;
        }
        risco::Placement placement;
        placement.cellId = song.bank[static_cast<size_t>(index)].id;
        placement.startBeat = value.getIntValue();
        placement.span = cellSpan.getSelectedId() == 1 ? risco::CellSpan::beat :
                         cellSpan.getSelectedId() == 4 ? risco::CellSpan::twoBars :
                         cellSpan.getSelectedId() == 3 ? risco::CellSpan::fourBars : risco::CellSpan::bar;
        placement.repeats = repetitions.getSelectedId();
        placement.stem = pendingStem;
        bool usedGenerativeRoute = false;
        if (autoRouteButton.getToggleState()) {
            const auto suggestion = risco::suggestPlacement(song, placement.cellId,
                placement.span, placement.repeats, placement.startBeat, true,
                routingSeedForCell(song.bank[static_cast<size_t>(index)]));
            if (!suggestion.valid) {
                status.setText(utf8("AUTO não encontrou uma entrada livre a partir do tempo indicado. Ajuste Tempo inicial ou desligue AUTO para inserir no tempo e STEM escolhidos."),
                               juce::dontSendNotification);
                return;
            }
            placement.startBeat = suggestion.startBeat;
            placement.stem = suggestion.stem;
            usedGenerativeRoute = true;
        }
        const auto before = captureGridState();
        if (!risco::placeCell(song, placement)) {
            status.setText(utf8("Não foi possível colar: confira posição e limite da grade."),
                           juce::dontSendNotification);
            return;
        }
        pushGridUndo(before);
        selectedPlacement = song.placementCount - 1;
        ensurePlacementVisible(selectedPlacement);
        startBeat.setText(juce::String(std::min(4096, placement.startBeat + placementBeats(placement))));
        refreshPlaybackAfterGridEdit();
        status.setText((usedGenerativeRoute ? utf8("O Risco escolheu a entrada · STEM ")
                                            : utf8("Célula colocada no STEM ")) +
                       juce::String(placement.stem + 1) +
                       (usedGenerativeRoute ? utf8(" · posição e canal derivados da entrada e da grade.") : "."),
                       juce::dontSendNotification);
        repaint();
    }
    void previewSelectedCell() {
        const int index = selectedCellIndex();
        if (index < 0) {
            status.setText(utf8("Escolha uma célula no banco para ouvi-la."), juce::dontSendNotification);
            return;
        }
        if (songSnapshots.size() >= 64) {
            status.setText(utf8("Limite de 64 reproduções nesta sessão; reinicie a maquete."),
                           juce::dontSendNotification);
            return;
        }
        auto preview = std::make_unique<risco::Song>();
        preview->bpm = song.bpm;
        preview->beatsPerBar = song.beatsPerBar;
        preview->compoundSixEight = song.compoundSixEight;
        preview->bitDepth = song.bitDepth;
        preview->timbreMode = song.timbreMode;
        preview->ayNoisePeriod = song.ayNoisePeriod;
        preview->ayEnvelopeMode = song.ayEnvelopeMode;
        preview->ayEnvelopeRamp = song.ayEnvelopeRamp;
        preview->historicalAyTexture = song.historicalAyTexture;
        preview->historicalAyNoisePeriod = song.historicalAyNoisePeriod;
        preview->historicalAyEnvelope = song.historicalAyEnvelope;
        preview->historicalAyEnvelopeRamp = song.historicalAyEnvelopeRamp;
        preview->bank[0] = song.bank[static_cast<size_t>(index)];
        preview->cellCount = 1;
        risco::Placement placement{preview->bank[0].id, 0,
            cellSpan.getSelectedId() == 1 ? risco::CellSpan::beat :
            cellSpan.getSelectedId() == 4 ? risco::CellSpan::twoBars :
            cellSpan.getSelectedId() == 3 ? risco::CellSpan::fourBars : risco::CellSpan::bar,
            repetitions.getSelectedId()};
        placement.stem = pendingStem;
        if (selectedPlacement >= 0 && selectedPlacement < song.placementCount) {
            const auto& selected = song.grid[static_cast<size_t>(selectedPlacement)];
            if (selected.cellId == placement.cellId) {
                placement = selected;
                placement.startBeat = 0;
            }
        }
        preview->grid[0] = placement;
        preview->placementCount = 1;
        auto next = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*preview));
        next->applyStemAutomation = false;
        if (next->count == 0) {
            status.setText(utf8("Não foi possível preparar a célula selecionada."),
                           juce::dontSendNotification);
            return;
        }
        if (next->placements[0].bytebeatFormula)
            next->placements[0].formulaTimeOffset = next->placements[0].sourceStart;
        const auto* pointer = next.get();
        songSnapshots.push_back(std::move(next));
        previewingCell = true;
        playing.store(false, std::memory_order_release);
        directEnabled.store(false, std::memory_order_release);
        directMode.setToggleState(false, juce::dontSendNotification);
        autoMode.setToggleState(false, juce::dontSendNotification);
        active.store(nullptr, std::memory_order_release);
        activeSong.store(pointer, std::memory_order_release);
        restart.store(true, std::memory_order_release);
        playing.store(true, std::memory_order_release);
        const auto& cell = song.bank[static_cast<size_t>(index)];
        if (cell.formulaOrigin.source == "risco:glitch-math-v1")
            status.setText(utf8("Ouvindo glitch derivado do pai #") +
                juce::String(cell.formulaOrigin.parentA) +
                utf8("; selecione esse pai no banco para retornar ao original."),
                juce::dontSendNotification);
        else
            status.setText(utf8("Ouvindo célula selecionada, fora da música completa."),
                           juce::dontSendNotification);
    }
    bool playArrangement(bool restartPlayback = true) {
        // Retire snapshots de reproduções anteriores antes do limite. A coleta
        // só apaga itens inativos quando o callback de áudio não os está lendo.
        pruneAudioSnapshots();
        if (songSnapshots.size() >= 64) {
            status.setText(utf8("Limite de 64 reproduções nesta sessão; reinicie a maquete."),
                           juce::dontSendNotification);
            return false;
        }
        auto next = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(song));
        if (next->count == 0) {
            status.setText(utf8("Grade vazia. Use Faixa completa para gerar uma música ou insira uma célula."),
                           juce::dontSendNotification);
            return false;
        }
        const auto* pointer = next.get();
        songSnapshots.push_back(std::move(next));
        previewingCell = false;
        directEnabled.store(false, std::memory_order_release);
        directMode.setToggleState(false, juce::dontSendNotification);
        autoMode.setToggleState(false, juce::dontSendNotification);
        if (restartPlayback) playing.store(false, std::memory_order_release);
        active.store(nullptr, std::memory_order_release);
        activeSong.store(pointer, std::memory_order_release);
        if (restartPlayback) restart.store(true, std::memory_order_release);
        playing.store(true, std::memory_order_release);
        status.setText(song.interStemResponse > 0
            ? utf8("Reproduzindo a grade · ") + juce::String(pointer->responseCount) + utf8(" respostas · ") +
              juce::String(pointer->responsesSkipped) + utf8(" trechos ocupados ignorados.") +
              (song.responsePolicy == 7 ? utf8(" · notas sem espaço: ") + juce::String(pointer->responseNotesRejected) +
                  (pointer->responseGapBudgetExceeded ? utf8(" · limite de análise atingido") : juce::String()) : juce::String()) +
              (pointer->responseCount > 0 && pointer->responses[0].sourceSection >= 0 ?
                  utf8(" · primeira: seção ") + juce::String(pointer->responses[0].sourceSection + 1) +
                  utf8(" → ") + juce::String((pointer->responses[0].sourceSection + 1) % 4 + 1) : juce::String()) +
              (song.responsePolicy >= 3 && pointer->responseCount > 0 ?
                  (pointer->responses[0].harmonicReferenceId > 0 ? utf8(" · harmonia #") + juce::String(pointer->responses[0].harmonicReferenceId) :
                      utf8(" · sem referência harmônica")) : juce::String())
            : utf8("Reproduzindo a grade da música."), juce::dontSendNotification);
        return true;
    }
    void chooseAudioExport(int selectedStem = -1) {
        if (exportJob && exportJob->isThreadRunning()) return;
        if (song.placementCount == 0) {
            status.setText(utf8("Insira células na grade antes de exportar."), juce::dontSendNotification);
            return;
        }
        auto initial = currentFile.existsAsFile() ? currentFile.withFileExtension(".wav") :
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("risco.wav");
        if (exportPreMaster)
            initial = initial.getSiblingFile(initial.getFileNameWithoutExtension() + "-premaster.wav");
        if (selectedStem >= 0)
            initial = initial.getSiblingFile(initial.getFileNameWithoutExtension() +
                "-stem-" + juce::String(selectedStem + 1) + ".wav");
        fileChooser = std::make_unique<juce::FileChooser>(
            selectedStem == -2 ? utf8("Cinco stems: escolha pasta e nome-base (sem sobrescrever existentes)") :
            selectedStem >= 0 ? utf8("Exportar Stem ") + juce::String(selectedStem + 1) :
                               utf8("Exportar WAV estéreo"), initial, "*.wav");
        juce::Component::SafePointer<MainComponent> safeThis(this);
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis, selectedStem](const juce::FileChooser& chooser) {
                if (!safeThis || chooser.getResult() == juce::File()) return;
                safeThis->exportJob = std::make_unique<SongExport>(
                    std::make_unique<risco::Song>(safeThis->song), chooser.getResult().withFileExtension(".wav"),
                    selectedStem, safeThis->exportBitDepth, safeThis->exportPreMaster);
                safeThis->exportJob->completed = [safeThis](juce::String result) {
                    if (safeThis) safeThis->status.setText(result, juce::dontSendNotification);
                };
                safeThis->exportJob->launchThread(juce::Thread::Priority::low);
            });
    }
    void chooseSongFile(bool saving) {
        const auto initial = currentFile.existsAsFile()
            ? currentFile
            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                  .getChildFile("risco.risco");
        fileChooser = std::make_unique<juce::FileChooser>(
            saving ? utf8("Salvar música Risco") : utf8("Abrir música Risco"),
            initial, "*.risco");
        juce::Component::SafePointer<MainComponent> safeThis(this);
        const int flags = (saving ? juce::FileBrowserComponent::saveMode |
                                     juce::FileBrowserComponent::warnAboutOverwriting
                                  : juce::FileBrowserComponent::openMode) |
                          juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [safeThis, saving](const juce::FileChooser& chooser) {
            if (safeThis == nullptr) return;
            auto file = chooser.getResult();
            if (file == juce::File()) return;
            if (saving) safeThis->saveToFile(file.withFileExtension(".risco"));
            else safeThis->loadFromFile(file);
        });
    }
    static juce::File recentSongsFile() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Risco").getChildFile("recent-projects.json");
    }
    static juce::File libraryCatalogFile() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Risco").getChildFile("project-library.json");
    }
    bool saveLibraryCatalog() const {
        ProjectLibraryCatalog catalog;
        catalog.sources = librarySourcePaths;
        catalog.groups = libraryGroups;
        return writeProjectLibraryCatalog(libraryCatalogFile(),catalog);
    }
    static juce::StringArray readRecentSongs() {
        const auto file = recentSongsFile();
        juce::StringArray recent;
        if (file.getSize() <= 0 || file.getSize() > 65536) return recent;
        const auto json = juce::JSON::parse(file.loadFileAsString());
        if (const auto* items = json.getArray()) {
            for (const auto& item : *items) {
                if (recent.size() == 8) break;
                if (!item.isString()) continue;
                const auto path = item.toString();
                if (juce::File::isAbsolutePath(path)) recent.addIfNotAlreadyThere(path);
            }
        }
        return recent;
    }
    static void rememberSong(const juce::File& file) {
        auto recent = readRecentSongs();
        const auto path = file.getFullPathName();
        recent.removeString(path);
        recent.insert(0, path);
        juce::Array<juce::var> items;
        for (int i = 0; i < std::min(8, recent.size()); ++i) items.add(recent[i]);
        const auto target = recentSongsFile();
        if (target.getParentDirectory().createDirectory().wasOk())
            target.replaceWithText(juce::JSON::toString(juce::var(items)));
    }
    void saveToFile(const juce::File& file) {
        std::ostringstream stream;
        std::vector<risco::NamedSongVersionRecord> versions;
        versions.reserve(compositionVersions.size());
        size_t versionPayloadBytes = 0;
        for (const auto& version : compositionVersions) {
            std::ostringstream snapshotStream;
            if (!risco::saveSong(snapshotStream,version.song)) {
                status.setText(utf8("Uma versão nomeada está inválida; nada foi salvo."),juce::dontSendNotification);
                return;
            }
            auto payload = snapshotStream.str();
            if (payload.size() > risco::maxNamedSongVersionBytes - versionPayloadBytes) {
                status.setText(utf8("As versões excedem o orçamento agregado de 3 MiB; remova algumas antes de salvar."),juce::dontSendNotification);
                return;
            }
            versionPayloadBytes += payload.size();
            versions.push_back({version.name.toStdString(),std::move(payload),version.parentIndex});
        }
        if (!risco::saveSong(stream,song,versions,activeCompositionVersion)) {
            status.setText(utf8("Música inválida: nada foi salvo."), juce::dontSendNotification);
            return;
        }
        const auto bytes = stream.str();
        if (bytes.size() > 4 * 1024 * 1024) {
            status.setText(utf8("Projeto e versões excedem o limite atual de 4 MiB; nada foi salvo."),juce::dontSendNotification);
            return;
        }
        if (!file.replaceWithData(bytes.data(), bytes.size())) {
            status.setText(utf8("Falha ao salvar o arquivo da música."), juce::dontSendNotification);
            return;
        }
        currentFile = file;
        rememberSong(file);
        status.setText(utf8("Música salva."), juce::dontSendNotification);
    }
    static juce::File recoveryFile() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Risco").getChildFile("recovery.risco");
    }
    bool hasRecoveryData() const {
        return song.cellCount > 0 || currentPhraseGenerated || inscription.count > 0 || ascii.getText().isNotEmpty() ||
            formulaInput.getText() != "t + (t >> 4)";
    }
    void queueRecovery() {
        if (recoveryPending || recoveryPool.getNumJobs() != 0 || !hasRecoveryData()) return;
        juce::var context(new juce::DynamicObject());
        auto* values = context.getDynamicObject();
        const int cell = selectedCellIndex();
        values->setProperty("selectedCellId", static_cast<juce::int64>(cell >= 0 ? song.bank[static_cast<size_t>(cell)].id : 0));
        values->setProperty("selectedPlacement", selectedPlacement);
        values->setProperty("gridZoomBeats", gridZoomBeats);
        values->setProperty("gridScrollBeat", gridScrollBeat);
        values->setProperty("pendingStem", pendingStem);
        values->setProperty("startBeat", startBeat.getText().getIntValue());
        juce::var history(new juce::DynamicObject());
        history.getDynamicObject()->setProperty("bankUndo", recoveryBankHistory(bankUndo));
        history.getDynamicObject()->setProperty("bankRedo", recoveryBankHistory(bankRedo));
        history.getDynamicObject()->setProperty("gridUndo", recoveryGridHistory(gridUndoHistory, gridUndoCount));
        values->setProperty("history", history);
        juce::var draft(new juce::DynamicObject());
        auto* fields = draft.getDynamicObject();
        fields->setProperty("text", ascii.getText());
        fields->setProperty("formula", formulaInput.getText());
        fields->setProperty("name", cellName.getText());
        fields->setProperty("seed", seedInput.getText());
        fields->setProperty("seedBeforeGlitch", juce::String(seedBeforeGlitch));
        fields->setProperty("seedSource", seedSource.getSelectedId());
        fields->setProperty("textMode", textEntryMode);
        fields->setProperty("inputInterpretation", inputInterpretationChoice);
        fields->setProperty("inputInterpretationStrength", inputInterpretationStrength);
        fields->setProperty("inputGestureRangeSemitones",inputGestureRangeSemitones);
        fields->setProperty("inputGestureCurveEnabled", inputGestureCurve.enabled);
        juce::Array<juce::var> gestureCurveValues;
        for (const float value : inputGestureCurve.values) gestureCurveValues.add(value);
        fields->setProperty("inputGestureCurve", juce::var(gestureCurveValues));
        fields->setProperty("scale", formulaScaleSelect.getSelectedId());
        fields->setProperty("motion", formulaMotionSelect.getSelectedId());
        fields->setProperty("tonic", formulaTonicSelect.getSelectedId());
        fields->setProperty("progression", formulaProgressionChoice);
        fields->setProperty("harmonicBars", formulaHarmonicBarsChoice);
        fields->setProperty("swing", formulaSwingChoice);
        fields->setProperty("subdivision", formulaSubdivisionChoice);
        fields->setProperty("syncopation", formulaSyncopationChoice);
        fields->setProperty("pulses", formulaPulsesChoice);
        juce::Array<juce::var> roots;
        for (const int root : formulaChordRootChoices) roots.add(root);
        fields->setProperty("chordRoots", juce::var(roots));
        fields->setProperty("generated", currentPhraseGenerated);
        fields->setProperty("phraseSeed", juce::String(currentPhraseSeed));
        fields->setProperty("phraseVariation", static_cast<juce::int64>(currentPhraseVariation));
        fields->setProperty("phraseVersion", int(currentPhraseVersion));
        fields->setProperty("phraseMask", int(currentPhraseSectionMask));
        fields->setProperty("sections", recoverySectionEdits(currentSectionEdits));
        juce::Array<juce::var> meters;
        for (int meter : currentPhraseMeters) meters.add(meter);
        fields->setProperty("meters", juce::var(meters));
        juce::Array<juce::var> sectionBars;
        for (int bars : currentPhraseSectionBars) sectionBars.add(bars);
        fields->setProperty("sectionBars", juce::var(sectionBars));
        fields->setProperty("compareOriginal", compareOriginalSections);
        juce::Array<juce::var> strokes;
        for (int s = 0; s < std::clamp(inscription.count, 0, 12); ++s) {
            const auto& stroke = inscription.strokes[static_cast<size_t>(s)];
            juce::var data(new juce::DynamicObject());
            data.getDynamicObject()->setProperty("crossed", stroke.crossed);
            juce::Array<juce::var> points;
            for (int p = 0; p < std::clamp(stroke.count, 0, 128); ++p) {
                const auto& point = stroke.points[static_cast<size_t>(p)];
                juce::Array<juce::var> coordinates;
                coordinates.add(double(point.x)); coordinates.add(double(point.y));
                points.add(juce::var(coordinates));
            }
            data.getDynamicObject()->setProperty("points", juce::var(points));
            strokes.add(data);
        }
        fields->setProperty("strokes", juce::var(strokes));
        values->setProperty("draft", draft);
        recoveryPool.addJob(new RecoveryJob(std::make_unique<risco::Song>(song), recoveryFile(), recoveryState, context), true);
    }
    bool loadFromFile(const juce::File& file, bool recovering = false) {
        constexpr std::int64_t maxSongBytes = 4 * 1024 * 1024;
        if (file.getSize() <= 0 || file.getSize() > maxSongBytes) {
            status.setText(utf8("Arquivo grande demais para esta maquete."), juce::dontSendNotification);
            return false;
        }
        juce::MemoryBlock bytes;
        if (!file.loadFileAsData(bytes)) {
            status.setText(utf8("Não foi possível ler a música."), juce::dontSendNotification);
            return false;
        }
        if (bytes.getSize() == 0 || bytes.getSize() > static_cast<std::size_t>(maxSongBytes)) {
            status.setText(utf8("Arquivo grande demais ou vazio."), juce::dontSendNotification);
            return false;
        }
        std::string projectBytes(static_cast<const char*>(bytes.getData()), bytes.getSize());
        juce::var recoveryContext;
        if (recovering) {
            std::string decoded;
            if (!decodeRecovery(projectBytes, decoded, recoveryContext)) {
                status.setText(utf8("Cópia de recuperação inválida; sessão atual preservada."), juce::dontSendNotification);
                return false;
            }
            projectBytes = std::move(decoded);
            if (!validRecoveryDraft(recoveryContext.getProperty("draft", {}))) {
                status.setText(utf8("Rascunho de recuperação inválido; sessão atual preservada."), juce::dontSendNotification);
                return false;
            }
        }
        std::istringstream stream(projectBytes);
        auto loaded = std::make_unique<risco::Song>();
        std::vector<risco::NamedSongVersionRecord> savedVersions;
        int savedActiveVersion = -1;
        if (!risco::loadSong(stream,*loaded,&savedVersions,&savedActiveVersion)) {
            status.setText(utf8("Arquivo Risco inválido; a música aberta foi preservada."),
                           juce::dontSendNotification);
            return false;
        }
        std::vector<CompositionVersion> restoredVersions;
        restoredVersions.reserve(savedVersions.size());
        for (const auto& savedVersion : savedVersions) {
            CompositionVersion version;
            version.name = juce::String::fromUTF8(savedVersion.name.c_str());
            version.parentIndex = savedVersion.parentIndex;
            std::istringstream snapshotStream(savedVersion.serializedSong);
            if (!risco::loadSong(snapshotStream,version.song)) {
                status.setText(utf8("Uma versão nomeada do arquivo está inválida; a música atual foi preservada."),
                               juce::dontSendNotification);
                return false;
            }
            restoredVersions.push_back(std::move(version));
        }
        if (recovering && restoredVersions.empty()) {
            restoredVersions = compositionVersions;
            savedActiveVersion = activeCompositionVersion;
        }
        playing.store(false, std::memory_order_release);
        previewingCell = false;
        directEnabled.store(false, std::memory_order_release);
        resetDirect.store(true, std::memory_order_release);
        directMode.setToggleState(false, juce::dontSendNotification);
        autoMode.setToggleState(false, juce::dontSendNotification);
        autoPhraseButton.setToggleState(false, juce::dontSendNotification);
        loopEnabled.store(false, std::memory_order_release);
        loopMode.setToggleState(false, juce::dontSendNotification);
        active.store(nullptr, std::memory_order_release);
        activeSong.store(nullptr, std::memory_order_release);
        activeFormula.store(nullptr, std::memory_order_release);
        currentPhraseGenerated = false;
        currentPhraseSeed = 0;
        currentPhraseVariation = 0;
        phraseVariation = 0;
        song = std::move(*loaded);
        compositionVersions = std::move(restoredVersions);
        activeCompositionVersion = savedActiveVersion;
        resetOutputLoudness.store(true, std::memory_order_release);
        formulaProgressionChoice = 0;
        formulaHarmonicBarsChoice = 1;
        formulaSwingChoice = 0.0;
        formulaSubdivisionChoice = 0;
        formulaSyncopationChoice = false;
        formulaPulsesChoice = 0;
        formulaChordRootChoices = {{-1, -1, -1, -1}};
        currentSectionEdits = {};
        currentPhraseMeters = {};
        currentPhraseSectionBars = {{1,1,1,1}};
        compareOriginalSections = false;
        inputInterpretationChoice = 0;
        inputInterpretationStrength = 1;
        inputGestureCurve = {};
        inputGestureRangeSemitones = 12;
        bankQuery.clear();
        bankFavoritesOnly = false;
        refreshMeterChoices();
        refreshMetricGroupingOptions();
        clearGridUndoHistory();
        for (int stem = 0; stem < risco::Song::stemCount; ++stem) {
            const float pan = song.stemPan[static_cast<size_t>(stem)];
            stemPan[static_cast<size_t>(stem)].setValue(pan, juce::dontSendNotification);
            stemPanValue[static_cast<size_t>(stem)].store(pan, std::memory_order_release);
            stemRoleSelect[static_cast<size_t>(stem)].setSelectedId(
                int(song.stemRole[static_cast<size_t>(stem)]) + 1, juce::dontSendNotification);
            const auto text = std::abs(pan) < 0.025f ? juce::String("C") :
                juce::String(pan < 0.0f ? "L" : "R") +
                juce::String(int(std::round(std::abs(pan) * 100.0f)));
            stemPanLabel[static_cast<size_t>(stem)].setText(text, juce::dontSendNotification);
            const float gain = song.stemGain[static_cast<size_t>(stem)];
            stemGain[static_cast<size_t>(stem)].setValue(gain, juce::dontSendNotification);
            stemGainValue[static_cast<size_t>(stem)].store(gain, std::memory_order_release);
            stemMute[static_cast<size_t>(stem)].setToggleState(false, juce::dontSendNotification);
            stemMuted[static_cast<size_t>(stem)].store(false, std::memory_order_release);
            stemSolo[static_cast<size_t>(stem)].setToggleState(false, juce::dontSendNotification);
            stemSoloed[static_cast<size_t>(stem)].store(false, std::memory_order_release);
            stemEffectValue[static_cast<size_t>(stem)].store(song.stemEffect[static_cast<size_t>(stem)], std::memory_order_release);
            stemEffects[static_cast<size_t>(stem)].setBitcrushBits(song.stemBitcrushBits[static_cast<size_t>(stem)]);
            stemBitcrushBitsValue[static_cast<size_t>(stem)].store(song.stemBitcrushBits[static_cast<size_t>(stem)], std::memory_order_release);
            stemFilterCutoffValue[static_cast<size_t>(stem)].store(song.stemFilterCutoff[static_cast<size_t>(stem)], std::memory_order_release);
            stemReverbMixValue[static_cast<size_t>(stem)].store(song.stemReverbMix[static_cast<size_t>(stem)], std::memory_order_release);
            stemReverbDecayValue[static_cast<size_t>(stem)].store(song.stemReverbDecay[static_cast<size_t>(stem)], std::memory_order_release);
            stemDelayMixValue[static_cast<size_t>(stem)].store(song.stemDelayMix[static_cast<size_t>(stem)], std::memory_order_release);
            stemDelayFeedbackValue[static_cast<size_t>(stem)].store(song.stemDelayFeedback[static_cast<size_t>(stem)], std::memory_order_release);
            const auto index = static_cast<size_t>(stem);
            for (size_t p = 0; p < 3; ++p)
                stemFilterAutoState[index].values[p].store(song.stemFilterAutomation[index].values[p], std::memory_order_release);
            stemFilterAutoState[index].enabled.store(song.stemFilterAutomation[index].enabled, std::memory_order_release);
            for (size_t p = 0; p < 3; ++p) {
                stemDelayAutoState[index].mix[p].store(song.stemDelayMixAutomation[index].values[p], std::memory_order_release);
                stemDelayAutoState[index].feedback[p].store(song.stemDelayFeedbackAutomation[index].values[p], std::memory_order_release);
            }
            stemDelayAutoState[index].mixEnabled.store(song.stemDelayMixAutomation[index].enabled, std::memory_order_release);
            stemDelayAutoState[index].feedbackEnabled.store(song.stemDelayFeedbackAutomation[index].enabled, std::memory_order_release);
            for (size_t p = 0; p < 3; ++p)
                stemReverbAutoState[index].mix[p].store(song.stemReverbMixAutomation[index].values[p], std::memory_order_release);
            stemReverbAutoState[index].enabled.store(song.stemReverbMixAutomation[index].enabled, std::memory_order_release);
            for (size_t p = 0; p < 3; ++p)
                stemReverbAutoState[index].decay[p].store(song.stemReverbDecayAutomation[index].values[p], std::memory_order_release);
            stemReverbAutoState[index].decayEnabled.store(song.stemReverbDecayAutomation[index].enabled, std::memory_order_release);
            for (size_t p = 0; p < 3; ++p)
                stemBitcrushAutoState[index].bits[p].store(song.stemBitcrushAutomation[index].values[p], std::memory_order_release);
            stemBitcrushAutoState[index].enabled.store(song.stemBitcrushAutomation[index].enabled, std::memory_order_release);
            for (size_t p = 0; p < 3; ++p) stemAutoState[index].levels[p].store(song.stemAutomation[index].levels[p], std::memory_order_release);
            stemAutoState[index].enabled.store(song.stemAutomation[index].enabled, std::memory_order_release);
            for (size_t p = 0; p < 3; ++p) {
                stemPanAutoState[index].levels[p].store(song.stemPanAutomation[index].levels[p],std::memory_order_release);
                stemFxAutoState[index].active[p].store(song.stemFxAutomation[index].active[p],std::memory_order_release);
            }
            stemPanAutoState[index].enabled.store(song.stemPanAutomation[index].enabled,std::memory_order_release);
            stemFxAutoState[index].enabled.store(song.stemFxAutomation[index].enabled,std::memory_order_release);
            stemFx[index].setToggleState(song.stemEffect[index] > 0 || song.stemAutomation[index].enabled ||
                song.stemPanAutomation[index].enabled || song.stemFxAutomation[index].enabled,juce::dontSendNotification);
        }
        pendingStem = 0;
        stemSelect.setSelectedId(1, juce::dontSendNotification);
        loopRangeStartBeat = loopRangeEndBeat = 0.0;
        loopRangeEnabled.store(false, std::memory_order_release);
        loopRangeButton.setToggleState(false, juce::dontSendNotification);
        loopRangeStartSeconds.store(0.0, std::memory_order_release);
        loopRangeEndSeconds.store(0.0, std::memory_order_release);
        selectedPlacement = -1;
        bpmSlider.setValue(song.bpm, juce::dontSendNotification);
        renderingBpm.store(song.bpm, std::memory_order_release);
        masterSlider.setValue(song.masterGain, juce::dontSendNotification);
        masterGain.store(song.masterGain, std::memory_order_release);
        refreshMeterChoices();
        responseModeSelect.setSelectedId(song.interStemResponse + 1, juce::dontSendNotification);
        bitDepthSelect.setSelectedId(song.bitDepth, juce::dontSendNotification);
        shapeSelect.setSelectedId(song.shapeMode + 1, juce::dontSendNotification);
        shapeDriveSlider.setValue(song.shapeDrive, juce::dontSendNotification);
        refreshTimbreChoices();
        renderingAyNoisePeriod.store(song.ayNoisePeriod, std::memory_order_release);
        renderingAyEnvelopeMode.store(song.ayEnvelopeMode, std::memory_order_release);
        renderingAyEnvelopeRamp.store(song.ayEnvelopeRamp, std::memory_order_release);
        renderingHistoricalAyTexture.store(song.historicalAyTexture, std::memory_order_release);
        renderingHistoricalAyNoisePeriod.store(song.historicalAyNoisePeriod, std::memory_order_release);
        renderingHistoricalAyEnvelope.store(song.historicalAyEnvelope, std::memory_order_release);
        renderingHistoricalAyEnvelopeRamp.store(song.historicalAyEnvelopeRamp, std::memory_order_release);
        renderingBits.store(song.bitDepth, std::memory_order_release);
        renderingTimbre.store(song.timbreMode, std::memory_order_release);
        renderingShape.store(song.shapeMode, std::memory_order_release);
        renderingDrive.store(song.shapeDrive, std::memory_order_release);
        const bool generatedTrack = song.compositionSeedRecipeEnabled &&
            std::any_of(song.bank.begin(), song.bank.begin() + song.cellCount, [](const risco::Cell& cell) {
                return risco::isSeedTrackOrigin(cell.formulaOrigin);
            });
        if (song.timbreMode > 0 || generatedTrack) {
            formulaEnabled.store(false, std::memory_order_release);
            functionMode.setToggleState(false, juce::dontSendNotification);
        }
        currentFile = recovering ? juce::File() : file;
        if (!recovering) rememberSong(file);
        else recoveryPending = false;
        startBeat.setText("0");
        int preferredIndex = 0;
        for (int i = 0; i < song.cellCount; ++i)
            if (song.bank[static_cast<size_t>(i)].id == song.preferredTakeId) { preferredIndex = i; break; }
        refreshBank(preferredIndex);
        if (song.preferredTakeId != 0 && currentPhraseGenerated) {
            cellSpan.setSelectedId(currentPhraseVersion == 4 ? 4 : 3,juce::dontSendNotification);
            repetitions.setSelectedId(1,juce::dontSendNotification);
        }
        if (recovering && recoveryContext.isObject()) {
            const auto history = recoveryContext.getProperty("history", {});
            if (history.isObject()) {
                readRecoveryBankHistory(history.getProperty("bankUndo", {}), song, bankUndo);
                readRecoveryBankHistory(history.getProperty("bankRedo", {}), song, bankRedo);
                readRecoveryGridHistory(history.getProperty("gridUndo", {}), song, gridUndoHistory, gridUndoCount);
                undoGridButton.setEnabled(gridUndoCount > 0);
            }
            const auto cellId = static_cast<juce::int64>(recoveryContext.getProperty("selectedCellId", 0));
            for (int i = 0; i < song.cellCount; ++i)
                if (static_cast<juce::int64>(song.bank[static_cast<size_t>(i)].id) == cellId) { refreshBank(i); break; }
            const int placement = int(recoveryContext.getProperty("selectedPlacement", -1));
            selectedPlacement = placement >= 0 && placement < song.placementCount ? placement : -1;
            gridZoomBeats = std::clamp(int(recoveryContext.getProperty("gridZoomBeats", 0)), 0, 4096);
            const double scroll = double(recoveryContext.getProperty("gridScrollBeat", 0.0));
            gridScrollBeat = std::isfinite(scroll) ? std::clamp(scroll, 0.0, 4096.0) : 0.0;
            gridScrollBeat = gridViewStart();
            pendingStem = std::clamp(int(recoveryContext.getProperty("pendingStem", 0)), 0, risco::Song::stemCount - 1);
            stemSelect.setSelectedId(pendingStem + 1, juce::dontSendNotification);
            startBeat.setText(juce::String(std::clamp(int(recoveryContext.getProperty("startBeat", 0)), 0, 4096)));
            const auto draft = recoveryContext.getProperty("draft", {});
            if (draft.isObject()) {
                inscription = {};
                const auto strokes = draft.getProperty("strokes", {});
                for (const auto& data : *strokes.getArray()) {
                    auto& stroke = inscription.strokes[static_cast<size_t>(inscription.count++)];
                    stroke.crossed = bool(data.getProperty("crossed", false));
                    const auto points = data.getProperty("points", {});
                    for (const auto& point : *points.getArray()) {
                        const auto* coordinates = point.getArray();
                        stroke.points[static_cast<size_t>(stroke.count++)] = {float(double((*coordinates)[0])), float(double((*coordinates)[1]))};
                    }
                }
                ascii.setText(draft.getProperty("text", "").toString(), juce::dontSendNotification);
                inscription.ascii = ascii.getText().toStdString();
                formulaInput.setText(draft.getProperty("formula", "").toString(), juce::dontSendNotification);
                cellName.setText(draft.getProperty("name", "").toString(), false);
                seedSource.setSelectedId(std::clamp(int(draft.getProperty("seedSource", 1)), 1, 6), juce::dontSendNotification);
                updateGlitchControls();
                seedBeforeGlitch = draft.getProperty("seedBeforeGlitch", "").toString().toStdString();
                seedInput.setText(draft.getProperty("seed", "").toString(), juce::dontSendNotification);
                currentPhraseGenerated = false;
                formulaScaleSelect.setSelectedId(std::clamp(int(draft.getProperty("scale", 1)), 1, 27), juce::dontSendNotification);
                formulaMotionSelect.setSelectedId(std::clamp(int(draft.getProperty("motion", 1)), 1, 7), juce::dontSendNotification);
                formulaTonicSelect.setSelectedId(std::clamp(int(draft.getProperty("tonic", 1)), 1, 12), juce::dontSendNotification);
                formulaProgressionChoice = std::clamp(int(draft.getProperty("progression", 0)), 0, 2);
                formulaHarmonicBarsChoice = std::clamp(int(draft.getProperty("harmonicBars", 1)), 1, 2);
                const double recoveredSwing = double(draft.getProperty("swing", 0.0));
                formulaSwingChoice = std::isfinite(recoveredSwing) ? std::clamp(recoveredSwing, 0.0, 0.5) : 0.0;
                const int division = int(draft.getProperty("subdivision", 0));
                formulaSubdivisionChoice = division == 1 || division == 2 || division == 3 || division == 4 || division == 6 ? division : 0;
                formulaSyncopationChoice = bool(draft.getProperty("syncopation", false));
                const int pulses = int(draft.getProperty("pulses", 0));
                formulaPulsesChoice = pulses == 2 || pulses == 3 || pulses == 5 || pulses == 7 ? pulses : 0;
                const auto roots = draft.getProperty("chordRoots", {});
                if (roots.isArray()) for (int i = 0; i < 4; ++i)
                    formulaChordRootChoices[static_cast<size_t>(i)] = int((*roots.getArray())[i]);
                setTextEntryMode(bool(draft.getProperty("textMode", true)), false, false);
                inputInterpretationChoice = std::clamp(int(draft.getProperty("inputInterpretation", 0)), 0, 13);
                inputInterpretationStrength = float(double(draft.getProperty("inputInterpretationStrength", 1.0)));
                inputGestureRangeSemitones = int(draft.getProperty("inputGestureRangeSemitones",12));
                if (!risco::validTemporalGestureRange(inputGestureRangeSemitones)) inputGestureRangeSemitones = 12;
                inputGestureCurve.enabled = bool(draft.getProperty("inputGestureCurveEnabled", false));
                const auto recoveredGestureCurve = draft.getProperty("inputGestureCurve", {});
                if (recoveredGestureCurve.isArray() && recoveredGestureCurve.getArray()->size() == 3)
                    for (int i = 0; i < 3; ++i)
                        inputGestureCurve.values[static_cast<size_t>(i)] = float(double((*recoveredGestureCurve.getArray())[i]));
                if (!risco::validTemporalGestureCurve(inputGestureCurve)) inputGestureCurve = {};
                if (inputGestureCurve.enabled && (inputInterpretationChoice < 9 || inputInterpretationChoice > 13)) inputInterpretationChoice = 8;
                if (bool(draft.getProperty("generated", false))) {
                    const auto seed = draft.getProperty("phraseSeed", "0").toString().toStdString();
                    std::from_chars(seed.data(), seed.data() + seed.size(), currentPhraseSeed);
                    currentPhraseVariation = static_cast<std::uint32_t>(static_cast<juce::int64>(draft.getProperty("phraseVariation", 0)));
                    currentPhraseVersion = selectedPhraseVersion = unsigned(int(draft.getProperty("phraseVersion", 2)));
                    currentPhraseSectionMask = std::uint8_t(int(draft.getProperty("phraseMask", 15)));
                    readRecoverySectionEdits(draft.getProperty("sections", {}), currentSectionEdits);
                    const auto meters = draft.getProperty("meters", {});
                    if (meters.isArray()) for (int i = 0; i < 4; ++i) currentPhraseMeters[static_cast<size_t>(i)] = int((*meters.getArray())[i]);
                    const auto sectionBars = draft.getProperty("sectionBars", {});
                    if (sectionBars.isArray()) for (int i = 0; i < 4; ++i)
                        currentPhraseSectionBars[static_cast<size_t>(i)] = std::clamp(int((*sectionBars.getArray())[i]),1,4);
                    compareOriginalSections = bool(draft.getProperty("compareOriginal", false));
                    phraseVariation = currentPhraseVariation + 1;
                    currentPhraseGenerated = true;
                }
            }
        }
        status.setText(recovering ? utf8("Cópia recuperada com transporte parado. Use Salvar para criar o projeto confirmado.") :
            utf8("Música aberta."), juce::dontSendNotification);
        if (recovering && bool(recoveryContext.getProperty("historyTruncated", false)))
            status.setText(utf8("Cópia recuperada; histórico de desfazer omitido para respeitar o limite de 4 MiB. Salvar confirma o projeto."), juce::dontSendNotification);
        repaint();
        return true;
    }
    void loadGlyph() {
        const auto text = ascii.getText();
        if (text.isEmpty() || text[0] <= 32) {
            status.setText(utf8("Digite um caractere visível para ler seu contorno."),
                           juce::dontSendNotification);
            return;
        }
        juce::GlyphArrangement arrangement;
        arrangement.addLineOfText(juce::Font(juce::FontOptions(220.0f)),
                                  text.substring(0, 1),
                                  0.0f, 0.0f);
        juce::Path path;
        arrangement.createPath(path);
        const auto bounds = path.getBounds();
        if (bounds.isEmpty()) {
            status.setText(utf8("Esta fonte não forneceu contorno para o caractere."),
                           juce::dontSendNotification);
            return;
        }
        risco::Inscription next;
        next.ascii = text.toStdString();
        juce::PathFlatteningIterator iterator(path, juce::AffineTransform(), 1.0f);
        int contour = 0;
        auto addPoint = [&](risco::Stroke& stroke, float x, float y) {
            if (stroke.count < int(stroke.points.size()))
                stroke.points[static_cast<size_t>(stroke.count++)] = {
                    std::clamp((x - bounds.getX()) / bounds.getWidth(), 0.0f, 1.0f),
                    std::clamp((y - bounds.getY()) / bounds.getHeight(), 0.0f, 1.0f)};
        };
        while (contour < int(next.strokes.size()) && iterator.next()) {
            auto& stroke = next.strokes[static_cast<size_t>(contour)];
            if (stroke.count == 0) addPoint(stroke, iterator.x1, iterator.y1);
            addPoint(stroke, iterator.x2, iterator.y2);
            if (iterator.isLastInSubpath()) {
                if (stroke.count >= 2) ++contour;
                else stroke = {};
            }
        }
        next.count = contour;
        if (next.count == 0) {
            status.setText(utf8("Nenhum contorno utilizável foi extraído."), juce::dontSendNotification);
            return;
        }
        playing.store(false, std::memory_order_release);
        inscription = next;
        updateLiveDrawing();
        status.setText(utf8("Contorno da fonte carregado. A ordem dos pontos não é caligrafia real."),
                       juce::dontSendNotification);
        repaint();
    }
    void pushDrawingUndo() {
        constexpr int capacity = 32;
        if (drawingUndoCount == capacity) {
            for (int i = 1; i < capacity; ++i)
                drawingUndoHistory[static_cast<size_t>(i - 1)] =
                    std::move(drawingUndoHistory[static_cast<size_t>(i)]);
            --drawingUndoCount;
        }
        drawingUndoHistory[static_cast<size_t>(drawingUndoCount++)] = inscription;
        for (int i = 0; i < drawingRedoCount; ++i)
            drawingRedoHistory[static_cast<size_t>(i)] = {};
        drawingRedoCount = 0;
        refreshDrawingHistoryButtons();
    }
    void refreshDrawingHistoryButtons() {
        undoDrawingButton.setEnabled(drawingUndoCount > 0 && !drawing);
        redoDrawingButton.setEnabled(drawingRedoCount > 0 && !drawing);
    }
    bool undoDrawingEdit() {
        if (drawing || drawingUndoCount <= 0) return false;
        constexpr int capacity = 32;
        if (drawingRedoCount == capacity) {
            for (int i = 1; i < capacity; ++i)
                drawingRedoHistory[static_cast<size_t>(i - 1)] =
                    std::move(drawingRedoHistory[static_cast<size_t>(i)]);
            --drawingRedoCount;
        }
        drawingRedoHistory[static_cast<size_t>(drawingRedoCount++)] = inscription;
        inscription = std::move(drawingUndoHistory[static_cast<size_t>(--drawingUndoCount)]);
        drawingUndoHistory[static_cast<size_t>(drawingUndoCount)] = {};
        ascii.setText(juce::String::fromUTF8(inscription.ascii.c_str()), juce::dontSendNotification);
        currentPhraseGenerated = false;
        updateLiveDrawing(false);
        refreshDrawingHistoryButtons();
        status.setText(utf8("Edição do desenho desfeita · som recalculado."), juce::dontSendNotification);
        repaint();
        return true;
    }
    bool redoDrawingEdit() {
        if (drawing || drawingRedoCount <= 0) return false;
        constexpr int capacity = 32;
        if (drawingUndoCount == capacity) {
            for (int i = 1; i < capacity; ++i)
                drawingUndoHistory[static_cast<size_t>(i - 1)] =
                    std::move(drawingUndoHistory[static_cast<size_t>(i)]);
            --drawingUndoCount;
        }
        drawingUndoHistory[static_cast<size_t>(drawingUndoCount++)] = inscription;
        inscription = std::move(drawingRedoHistory[static_cast<size_t>(--drawingRedoCount)]);
        drawingRedoHistory[static_cast<size_t>(drawingRedoCount)] = {};
        ascii.setText(juce::String::fromUTF8(inscription.ascii.c_str()), juce::dontSendNotification);
        currentPhraseGenerated = false;
        updateLiveDrawing(false);
        refreshDrawingHistoryButtons();
        status.setText(utf8("Edição do desenho refeita · som recalculado."), juce::dontSendNotification);
        repaint();
        return true;
    }
    static void compactStroke(risco::Stroke& stroke) noexcept {
        if (stroke.count < 4) return;
        const auto pressureField = [](const risco::Point& point) noexcept {
            return point.pressure >= 0.0f && point.pressure <= 1.0f
                ? point.pressure : 1.0f - point.y;
        };
        int removeAt = 1;
        float leastImportance = std::numeric_limits<float>::max();
        for (int i = 1; i < stroke.count - 1; ++i) {
            const auto& before = stroke.points[static_cast<size_t>(i - 1)];
            const auto& point = stroke.points[static_cast<size_t>(i)];
            const auto& after = stroke.points[static_cast<size_t>(i + 1)];
            const float dx = after.x - before.x;
            const float dy = after.y - before.y;
            const float lengthSquared = dx * dx + dy * dy;
            const float t = lengthSquared > 1.0e-8f
                ? std::clamp(((point.x - before.x) * dx + (point.y - before.y) * dy) /
                                 lengthSquared, 0.0f, 1.0f)
                : 0.5f;
            const float geometricError = std::hypot(point.x - (before.x + dx * t),
                                                     point.y - (before.y + dy * t));
            const float expectedPressure = pressureField(before) * (1.0f - t) +
                                           pressureField(after) * t;
            const float pressureError = std::abs(pressureField(point) - expectedPressure);
            const float importance = geometricError + 0.18f * pressureError;
            if (importance < leastImportance) {
                leastImportance = importance;
                removeAt = i;
            }
        }
        for (int i = removeAt; i < stroke.count - 1; ++i)
            stroke.points[static_cast<size_t>(i)] = stroke.points[static_cast<size_t>(i + 1)];
        stroke.points[static_cast<size_t>(--stroke.count)] = {};
    }

    void appendPoint(juce::Point<int> p, bool finish = false, float pressure = -1.0f) {
        if (inscription.count == 0) return;
        auto& stroke = inscription.strokes[static_cast<size_t>(inscription.count - 1)];
        const auto area = drawingArea();
        const float x = std::clamp(float(p.x - area.getX()) / float(area.getWidth()), 0.0f, 1.0f);
        const float y = std::clamp(float(p.y - area.getY()) / float(area.getHeight()), 0.0f, 1.0f);
        bool added = false;
        const bool newPosition = stroke.count == 0 ||
            std::abs(stroke.points[static_cast<size_t>(stroke.count - 1)].x - x) > 0.001f ||
            std::abs(stroke.points[static_cast<size_t>(stroke.count - 1)].y - y) > 0.001f;
        if (newPosition) {
            if (stroke.count >= int(stroke.points.size())) compactStroke(stroke);
            if (stroke.count < int(stroke.points.size())) {
                stroke.points[static_cast<size_t>(stroke.count++)] = {x, y, pressure};
                added = true;
            }
        } else if (pressure >= 0.0f && pressure <= 1.0f) {
            auto& last = stroke.points[static_cast<size_t>(stroke.count - 1)];
            if (last.pressure < 0.0f || std::abs(last.pressure - pressure) >= 0.02f) {
                last.pressure = pressure;
                added = true;
            }
        }
        const auto now = juce::Time::getMillisecondCounter();
        if (finish || (added && (stroke.count == 2 || now - lastLiveDrawingUpdateMillis >= 50))) {
            updateLiveDrawing();
            lastLiveDrawingUpdateMillis = now;
        }
        repaint();
    }
    risco::Inscription activeInput() const {
        auto source = inscription;
        source.ascii = ascii.getText().toStdString();
        if (textEntryMode) {
            source.count = 0;
            source.strokes = {};
        } else {
            source.ascii.clear();
        }
        return source;
    }
    void refreshInputButtonLabel() {
        ledSourceButton.setButtonText(textEntryMode
            ? uiText("Desenhar", "Draw", "Dessiner", "Dibujar")
            : uiText("Digitar texto", "Type text", "Saisir texte", "Escribir texto"));
    }

    void setTextEntryMode(bool enabled, bool focus, bool updateSound = true) {
        textEntryMode = enabled;
        marqueeScrollStartMillis = juce::Time::getMillisecondCounter();
        ascii.setVisible(enabled);
        ledShowsDrawing = !enabled;
        refreshInputButtonLabel();
        ledSourceButton.setTooltip(enabled
            ? utf8("Volta ao desenho neste mesmo quadro; o texto continua guardado.")
            : uiText("Desenhe com mouse ou caneta. Pressão compatível controla intensidade; altura também atua como pressão. Traços longos preservam curvas e pressão ao simplificar pontos. Alt+arraste desloca traços.",
                     "Draw with a mouse or pen. Supported pen pressure shapes dynamics; vertical position also acts as pressure. Long strokes simplify redundant points while preserving curves and pressure. Alt+drag moves strokes.",
                     "Dessinez à la souris ou au stylet. La pression prise en charge module l’intensité ; la hauteur agit aussi sur la pression. Les longs traits simplifient les points redondants en préservant courbes et pression. Alt-glisser déplace les traits.",
                     "Dibuje con ratón o lápiz. La presión compatible modela la intensidad; la altura también actúa como presión. Los trazos largos simplifican puntos redundantes y conservan curvas y presión. Alt+arrastrar desplaza trazos."));
        status.setText(enabled
            ? uiText("Digite no quadro ENTRADA ÚNICA; o som muda enquanto você escreve.", "Type in the input area; sound changes as you write.", "Saisissez dans la zone d’entrée ; le son suit l’écriture.", "Escriba en el área de entrada; el sonido cambia al escribir.")
            : uiText("Desenhe até 12 traços; o som responde enquanto você desenha.", "Draw up to 12 strokes; sound responds as you draw.", "Dessinez jusqu’à 12 traits ; le son répond au geste.", "Dibuje hasta 12 trazos; el sonido responde al gesto."),
            juce::dontSendNotification);
        if (focus) {
            if (enabled) ascii.grabKeyboardFocus();
            else grabKeyboardFocus();
        }
        if (updateSound) updateLiveDrawing(true);
    }
    void updateLiveDrawing() {
        updateLiveDrawing(false);
    }
    void updateLiveDrawing(bool resetScan) {
        const auto* baseArrangement = activeSong.load(std::memory_order_acquire);
        const auto* baseSequence = active.load(std::memory_order_acquire);
        const bool baseTransportPlaying = playing.load(std::memory_order_acquire) &&
            (baseArrangement != nullptr || baseSequence != nullptr);
        const bool continuingLiveAudio = baseTransportPlaying ||
            (playing.load(std::memory_order_acquire) &&
             liveInputPlayback.load(std::memory_order_acquire) != nullptr);
        const auto text = ascii.getText().toStdString();
        inscription.ascii = text;
        const auto sourceInput = activeInput();
        const int strokeCount = std::clamp(sourceInput.count, 0, int(sourceInput.strokes.size()));
        const bool hasStroke = std::any_of(sourceInput.strokes.begin(), sourceInput.strokes.begin() + strokeCount,
            [](const risco::Stroke& stroke) { return stroke.count >= 2; });
        const bool hasText = !sourceInput.ascii.empty();
        if (!hasStroke && !hasText && !currentPhraseGenerated) {
            liveInputPlayback.store(nullptr, std::memory_order_release);
            if (!baseTransportPlaying && !directEnabled.load(std::memory_order_acquire)) {
                playing.store(false, std::memory_order_release);
                previewingCell = false;
            }
            return;
        }
        if (hasStroke && !risco::validInscription(sourceInput)) return;
        if (songSnapshots.size() >= 1024) {
            status.setText(utf8("Limite de atualizações ao vivo nesta sessão; reinicie a maquete."),
                           juce::dontSendNotification);
            return;
        }
        const auto sourceSeed = hasStroke ? risco::formulaSeedFromDrawing(sourceInput)
                                          : risco::formulaSeedFromText(sourceInput.ascii);
        if (sourceSeed != liveInputSeed) {
            liveInputSeed = sourceSeed;
            liveDerivationIndex = 0;
            liveCompletedLoops = 0;
        }
        risco::CellPlan plan;
        plan.bpm = song.bpm;
        plan.beatsPerBar = song.beatsPerBar;
        plan.span = currentPhraseGenerated ? (currentPhraseVersion == 4 ? risco::CellSpan::twoBars : risco::CellSpan::fourBars) :
                    cellSpan.getSelectedId() == 1 ? risco::CellSpan::beat :
                    cellSpan.getSelectedId() == 4 ? risco::CellSpan::twoBars :
                    cellSpan.getSelectedId() == 3 ? risco::CellSpan::fourBars : risco::CellSpan::bar;
        plan.repeats = 1;
        int liveInterpretation = inputInterpretationChoice;
        float liveInterpretationStrength = inputInterpretationStrength;
        auto liveGestureCurve = inputGestureCurve;
        int liveGestureRangeSemitones = inputGestureRangeSemitones;
        if (!currentPhraseGenerated && autoRouteButton.getToggleState()) {
            const auto seed = sourceSeed ^ (std::uint64_t(liveDerivationIndex) * 0x9e3779b97f4a7c15ULL);
            constexpr int interpretations[] = {0, 0, 7, 7, 11, 12, 13, 0};
            liveInterpretation = interpretations[static_cast<std::size_t>((seed >> 27u) & 7u)];
            liveInterpretationStrength = (0.35f + float((seed >> 35u) & 7u) * 0.05f) *
                                         (0.65f + 0.35f * autoEvolutionAmount());
            if (liveInterpretation >= 11) {
                constexpr int ranges[] = {6, 12, 24};
                liveGestureRangeSemitones = ranges[static_cast<std::size_t>((seed >> 39u) % 3u)];
                liveGestureCurve.enabled = true;
                liveGestureCurve.values[0] = 0.0f;
                liveGestureCurve.values[1] = 0.4f + float((seed >> 42u) & 3u) * 0.07f;
                liveGestureCurve.values[2] = 1.0f;
            }
        }
        auto source = currentPhraseGenerated
            ? risco::phraseFromSeed(currentPhraseSeed, currentPhraseVariation,
                                    currentPhraseVersion, song.bpm, currentPhraseSectionMask, song.beatsPerBar, song.compoundSixEight, effectivePhraseMeters())
            : risco::interpretInput(risco::composeInput(sourceInput),liveInterpretation,
                liveInterpretationStrength,liveGestureCurve,liveGestureRangeSemitones);
        if (!currentPhraseGenerated && autoRouteButton.getToggleState() && liveDerivationIndex > 0)
            source = risco::deriveLiveVariation(source, sourceSeed, liveDerivationIndex, song.bpm,
                                                autoEvolutionAmount());
        if (currentPhraseGenerated && currentPhraseVersion < 6) source = risco::arrange(source, plan);
        if (currentPhraseGenerated) source = risco::editSections(source,
            compareOriginalSections ? risco::SectionEdits{} : currentSectionEdits, currentPhraseVersion == 4 ? 2 : 4,
            currentPhraseVersion >= 6 ? risco::phraseSectionWeights(effectivePhraseMeters(), effectivePhraseSectionBars(), song.beatsPerBar) : effectivePhraseSectionBars());
        if (source.end <= 0.0) return;
        auto next = std::make_unique<risco::SongPlayback>();
        next->end = source.end;
        next->bitDepth = song.bitDepth == 16 ? 16 : 8;
        next->timbreMode = std::clamp(song.timbreMode, 0, 6);
        next->ayNoisePeriod = std::clamp(song.ayNoisePeriod, 1, 31);
        next->ayEnvelopeMode = std::clamp(song.ayEnvelopeMode, 0, 4);
        next->ayEnvelopeRamp = song.ayEnvelopeRamp;
        next->historicalAySettings = {song.historicalAyTexture, song.historicalAyNoisePeriod,
                                      song.historicalAyEnvelope, song.historicalAyEnvelopeRamp};
        const bool generativeRouting = autoRouteButton.getToggleState();
        const auto addLivePlacement = [&](const risco::Sequence& sequence, int stem) {
            if (sequence.count <= 0 || next->count >= int(next->placements.size()) ||
                stem < 0 || stem >= risco::Song::stemCount) return;
            auto& placement = next->placements[static_cast<size_t>(next->count++)];
            placement.cell = sequence;
            placement.stem = stem;
            placement.start = 0.0;
            placement.duration = sequence.end;
            placement.playLength = sequence.end;
            placement.repeats = 1;
        };
        if (generativeRouting) {
            constexpr std::array<risco::VoiceRole, 3> voices{{
                risco::VoiceRole::lead, risco::VoiceRole::bass, risco::VoiceRole::harmony}};
            constexpr std::array<risco::CellRole, 3> roles{{
                risco::CellRole::melody, risco::CellRole::bass, risco::CellRole::harmony}};
            const auto stemForRole = [this, sourceSeed](risco::CellRole role, std::uint32_t salt) {
                const auto routeSeed = sourceSeed ^
                    (std::uint64_t(liveDerivationIndex + salt) * 0x94d049bb133111ebULL);
                std::array<int, risco::Song::stemCount> matches{};
                int matchCount = 0;
                for (int stem = 0; stem < risco::Song::stemCount; ++stem)
                    if (song.stemRole[static_cast<size_t>(stem)] == role)
                        matches[static_cast<size_t>(matchCount++)] = stem;
                return matchCount > 0 ? matches[static_cast<size_t>(routeSeed % unsigned(matchCount))]
                                      : int(routeSeed % risco::Song::stemCount);
            };
            for (size_t roleIndex = 0; roleIndex < voices.size(); ++roleIndex) {
                risco::Sequence part;
                part.end = source.end;
                for (int n = 0; n < source.count && part.count < int(part.notes.size()); ++n) {
                    const auto& note = source.notes[static_cast<size_t>(n)];
                    if (note.role == voices[roleIndex])
                        part.notes[static_cast<size_t>(part.count++)] = note;
                }
                addLivePlacement(part, stemForRole(roles[roleIndex], static_cast<std::uint32_t>(roleIndex)));
            }
            // Give the two additional mix lanes distinct live material too:
            // a bounded, seed-shaped pulse pattern and a sparse sustained halo.
            const float layerGain = autoEvolutionAmount();
            const double beat = 60.0 / std::clamp(song.bpm, 40.0, 240.0);
            risco::Sequence rhythm;
            rhythm.end = source.end;
            const double pulseStep = beat * 0.5;
            const int sourceEvents = std::clamp(source.count, 0, int(source.notes.size()));
            const int pulseSlots = sourceEvents > 0 ? sourceEvents :
                std::clamp(int(std::ceil(source.end / pulseStep)), 1, int(rhythm.notes.size()));
            for (int slot = 0; slot < pulseSlots; ++slot) {
                const auto random = sourceSeed ^ (std::uint64_t(liveDerivationIndex + 1u) *
                    0x9e3779b97f4a7c15ULL) ^ (std::uint64_t(slot + 1) * 0xbf58476d1ce4e5b9ULL);
                const risco::Note* anchor = sourceEvents > 0
                    ? &source.notes[static_cast<size_t>(slot)] : nullptr;
                const float sourceEnergy = anchor != nullptr
                    ? std::clamp(anchor->gain / 0.16f, 0.0f, 1.0f) : 0.0f;
                const bool accent = anchor != nullptr
                    ? (anchor->role == risco::VoiceRole::bass || sourceEnergy >= 0.65f || slot % 4 == 0)
                    : (slot % 2 == 0);
                if (!accent && (random & 3u) == 0u) continue;
                const double onset = anchor != nullptr ? anchor->start : slot * pulseStep;
                const double subdivision = beat * 0.25;
                const double quantized = std::round(std::max(0.0, onset) / subdivision) * subdivision;
                const int microOffset = int((random >> 8u) % 5u) - 2;
                const double start = std::clamp(quantized + double(microOffset) * subdivision * 0.25,
                                                0.0, std::max(0.0, source.end - 0.02));
                bool crowded = false;
                for (int existing = 0; existing < rhythm.count; ++existing)
                    if (std::abs(rhythm.notes[static_cast<size_t>(existing)].start - start) < beat * 0.04) {
                        crowded = true;
                        break;
                    }
                if (crowded) continue;
                const double duration = std::min(accent ? 0.11 : 0.075,
                                                 std::max(0.0, source.end - start));
                if (duration <= 0.0) continue;
                const double frequency = anchor != nullptr && anchor->frequency > 0.0
                    ? std::clamp(anchor->frequency * 0.5, 45.0, 110.0)
                    : accent ? 55.0 + double((random >> 16u) % 3u) * 11.0
                             : 92.0 + double((random >> 16u) % 5u) * 9.0;
                const float dynamics = (accent ? 0.085f : 0.045f) *
                    (anchor != nullptr ? 0.75f + sourceEnergy * 0.5f : 1.0f);
                rhythm.notes[static_cast<size_t>(rhythm.count++)] = {
                    start, duration, frequency, dynamics * layerGain,
                    risco::VoiceRole::bass, 0.125f};
            }
            if (rhythm.count > 0)
                addLivePlacement(rhythm, stemForRole(risco::CellRole::pulse, 3u));

            risco::Sequence texture;
            texture.end = source.end;
            const int textureStride = std::max(1, source.count / 8);
            for (int i = 0; i < source.count && texture.count < 16; i += textureStride) {
                const auto& note = source.notes[static_cast<size_t>(i)];
                if (note.frequency <= 0.0 || note.start >= source.end) continue;
                const auto random = sourceSeed ^ (std::uint64_t(liveDerivationIndex + 5u) *
                    0x94d049bb133111ebULL) ^ std::uint64_t(i + 1);
                const double start = std::max(0.0, note.start);
                const double duration = std::min(source.end - start,
                    std::max(note.duration * 1.8, beat * (1.0 + double((random >> 8u) & 1u))));
                const double frequency = note.frequency * ((random & 1u) != 0u ? 0.5 : 2.0);
                const float gain = std::min(0.035f, std::max(0.012f, note.gain * 0.22f)) * layerGain;
                texture.notes[static_cast<size_t>(texture.count++)] = {
                    start, duration, frequency, gain, risco::VoiceRole::harmony, 0.5f};
            }
            if (texture.count > 0)
                addLivePlacement(texture, stemForRole(risco::CellRole::texture, 4u));
        } else addLivePlacement(source, std::clamp(pendingStem, 0, risco::Song::stemCount - 1));
        if (next->count <= 0) return;
        const auto* pointer = next.get();
        songSnapshots.push_back(std::move(next)); // Snapshot imutável; vida útil cobre o callback de áudio.
        directEnabled.store(false, std::memory_order_release);
        directMode.setToggleState(false, juce::dontSendNotification);
        autoMode.setToggleState(false, juce::dontSendNotification);
        liveInputPlayback.store(pointer, std::memory_order_release);
        if (!baseTransportPlaying) previewingCell = false;
        liveDuration.store(pointer->end, std::memory_order_relaxed);
        liveObservedPlayhead = playheadSeconds.load(std::memory_order_relaxed);
        liveObservedCycle = baseArrangement != nullptr && pointer->end > 0.0
            ? static_cast<std::int64_t>(std::floor(liveObservedPlayhead / pointer->end)) : 0;
        if ((!baseTransportPlaying && resetScan) || !continuingLiveAudio)
            restart.store(true, std::memory_order_release);
        if (!baseTransportPlaying) {
            loopEnabled.store(true, std::memory_order_release);
            loopMode.setToggleState(true, juce::dontSendNotification);
        }
        playing.store(true, std::memory_order_release);
        if (hasStroke) {
            const auto seed = risco::formulaSeedFromDrawing(sourceInput);
            const auto palette = risco::paletteForDrawingSeed(seed);
            const auto root = risco::rootForDrawingSeed(seed);
            status.setText(utf8("Campo do desenho: ") + utf8(risco::pitchClassName(root))
                + " " + utf8(risco::paletteName(palette))
                + (generativeRouting ? utf8(" · AUTO deriva gesto e distribui vozes nos canais")
                                     : utf8(" · manual · STEM ") + juce::String(pendingStem + 1)),
                juce::dontSendNotification);
        } else {
            status.setText(generativeRouting
                ? utf8("Texto ao vivo · AUTO deriva melodia, baixo, harmonia, ritmo e textura.")
                : utf8("Texto ao vivo · manual · STEM ") + juce::String(pendingStem + 1),
                           juce::dontSendNotification);
        }
    }
    void playInscription() {
        inscription.ascii = ascii.getText().toStdString();
        const auto sourceInput = activeInput();
        if (!risco::validInscription(sourceInput)) {
            status.setText(utf8("A entrada ativa está vazia ou incompleta."),
                           juce::dontSendNotification);
            return;
        }
        currentPhraseGenerated = false;
        updateLiveDrawing(true);
    }
    void timerCallback() override {
        pruneAudioSnapshots();
        finishLibraryRead();
        const auto* livePlayback = liveInputPlayback.load(std::memory_order_acquire);
        const bool liveAutoPlaying = livePlayback != nullptr &&
            playing.load(std::memory_order_acquire) && autoRouteButton.getToggleState() &&
            !currentPhraseGenerated && !seedTrackPreview;
        if (liveAutoPlaying) {
            const double position = playheadSeconds.load(std::memory_order_relaxed);
            const double duration = std::max(0.18, liveDuration.load(std::memory_order_relaxed));
            const bool arrangementActive = activeSong.load(std::memory_order_acquire) != nullptr;
            const auto cycle = arrangementActive
                ? static_cast<std::int64_t>(std::floor(std::max(0.0, position) / duration)) : 0;
            const bool completedCycle = arrangementActive ? cycle > liveObservedCycle
                : liveObservedPlayhead > position + duration * 0.45;
            if (completedCycle) {
                ++liveCompletedLoops;
                if (liveCompletedLoops % static_cast<std::uint32_t>(std::max(1, autoEvolutionEveryLoops)) == 0u) {
                    ++liveDerivationIndex;
                    updateLiveDrawing(false);
                }
            }
            liveObservedPlayhead = playheadSeconds.load(std::memory_order_relaxed);
            liveObservedCycle = arrangementActive ? cycle : 0;
        } else {
            liveObservedPlayhead = playheadSeconds.load(std::memory_order_relaxed);
            liveObservedCycle = 0;
        }
        const auto recoveryNow = juce::Time::getMillisecondCounter();
        if (recoveryNow - lastRecoveryMillis >= 60000u) {
            lastRecoveryMillis = recoveryNow;
            queueRecovery();
        }
        if (recoveryState->notification.exchange(0) == 2)
            status.setText(utf8("Falha na cópia automática. Use Salvar para conservar a música."), juce::dontSendNotification);
        if (seedTrackReviewDialog) {
            auto* button = seedTrackReviewDialog->getButton(0);
            const auto label = seedTrackPreviewPlaying.load(std::memory_order_relaxed)
                ? uiText("Parar prévia", "Stop preview", "Arrêter l’aperçu", "Detener vista previa")
                : uiText("Ouvir prévia", "Play preview", "Écouter l’aperçu", "Oír vista previa");
            if (button->getButtonText() != label) button->setButtonText(label);
        }
        if (!seedTrackPreview && playing.load(std::memory_order_acquire) && !previewingCell &&
            activeSong.load(std::memory_order_acquire) != nullptr) {
            const double span = double(gridViewSpan());
            const double start = gridViewStart();
            const double beat = std::max(0.0,
                playheadSeconds.load(std::memory_order_relaxed) * song.bpm / 60.0);
            double target = start;
            if (beat < start + span * 0.20)
                target = beat - span * 0.30;
            else if (beat > start + span * 0.75)
                target = beat - span * 0.70;
            if (target != start) {
                const double maxStart = double(std::max(0, gridTotalBeats() - gridViewSpan()));
                const double clampedTarget = std::clamp(target, 0.0, maxStart);
                gridScrollBeat += (clampedTarget - start) * 0.25;
            }
        }
        const auto currentTick = displayedDirectTime.load(std::memory_order_relaxed);
        if (!seedTrackPreview && autoMode.getToggleState() && directEnabled.load(std::memory_order_acquire) &&
            currentTick >= nextAutoTick) {
            nextAutoTick = currentTick + phraseCycleTicks();
            if (!generateFormulaFromSeed())
                autoMode.setToggleState(false, juce::dontSendNotification);
        }
        const auto now = juce::Time::getMillisecondCounter();
        if (!seedTrackPreview && autoPhraseButton.getToggleState() &&
            static_cast<std::int32_t>(now - nextPhraseMillis) >= 0) {
            if (generatePhraseFromSeed(phraseVariation++)) {
                if (!appendGeneratedPhrase()) {
                    autoPhraseButton.setToggleState(false, juce::dontSendNotification);
                } else nextPhraseMillis = now + phraseCycleMillis();
            }
            else autoPhraseButton.setToggleState(false, juce::dontSendNotification);
        }
        refreshToggleDisplays();
        refreshFormulaGridStatus();
        if (albumRenderJob && albumRenderJob->isThreadRunning())
            showActivity(albumRenderJob->activityText());
        refreshGenerationStatus();
        repaint();
    }

    void showActivity(const juce::String& message) {
        activityMessage = message;
        refreshActivityLabel();
    }

    void refreshActivityLabel() {
        const auto idle = uiText("Aguardando tarefa", "Waiting for a task", "En attente d’une tâche", "Esperando tarea");
        activityStatus.setText(uiText("Atividade · ", "Activity · ", "Activité · ", "Actividad · ")
            + (activityMessage.isEmpty() ? idle : activityMessage), juce::dontSendNotification);
    }

    void pruneAudioSnapshots() {
        const bool audioIdle = audioReaders.load(std::memory_order_acquire) == 0;
        risco::pruneInactiveAudioSnapshots(snapshots, active.load(std::memory_order_acquire),
                                           audioIdle, 96);
        // Reproduções normais têm limite de 64; limpar em 33 evita que o
        // limite de sessão seja alcançado antes da coleta do timer.
        risco::pruneInactiveAudioSnapshots(songSnapshots, activeSong.load(std::memory_order_acquire),
                                           audioIdle, 32);
    }

    void refreshGenerationStatus() {
        const auto phrases = autoPhraseButton.getToggleState() ? "auto" : "manual";
        const auto formula = !autoMode.getToggleState() ? juce::String("manual")
            : directEnabled.load(std::memory_order_relaxed) ? juce::String("auto")
                : uiText("em espera", "waiting", "en attente", "en espera");
        const juce::String levels[] = {"manual",
            uiText("assistida", "assisted", "assistée", "asistida"),
            uiText("conjunto", "combined", "combinée", "combinada"),
            uiText("ampla", "broad", "large", "amplia")};
        const int level = song.compositionSeedRecipeEnabled
            ? std::clamp(int(song.compositionSeedLevel), 0, 3) : 0;
        generationStatus.setText(uiText("Frases: ", "Phrases: ", "Phrases : ", "Frases: ") + phrases
            + uiText(" · Fórmula: ", " · Formula: ", " · Formule : ", " · Fórmula: ") + formula
            + uiText(" · Receita: ", " · Recipe: ", " · Recette : ", " · Receta: ") + levels[level],
            juce::dontSendNotification);
    }

    void refreshAutomationEditor() {
        const int stem = std::clamp(automationEditor.selectedStem(), 0, risco::Song::stemCount - 1);
        const auto curve = static_cast<AutomationCurveChoice>(automationEditor.selectedCurve());
        const auto index = static_cast<size_t>(stem);
        std::array<float, 3> values{{1.0f, 1.0f, 1.0f}};
        std::array<juce::String, 3> labels{{"100%", "100%", "100%"}};
        bool enabled = false;
        juce::String title;
        juce::String range;
        const auto percent = [](float value, float maximum = 1.0f) {
            return juce::String(int(std::lround(std::clamp(value / maximum, 0.0f, 1.0f) * 100.0f))) + "%";
        };
        switch (curve) {
            case AutomationCurveChoice::volume:
                enabled = song.stemAutomation[index].enabled;
                for (size_t p = 0; p < 3; ++p) {
                    values[p] = std::clamp(song.stemAutomation[index].levels[p], 0.0f, 1.0f);
                    labels[p] = percent(values[p]);
                }
                title = utf8("Volume · Stem ") + juce::String(stem + 1); range = "0–100%"; break;
            case AutomationCurveChoice::pan:
                enabled = song.stemPanAutomation[index].enabled;
                for (size_t p = 0; p < 3; ++p) {
                    const float pan = std::clamp(song.stemPanAutomation[index].levels[p], -1.0f, 1.0f);
                    values[p] = pan * 0.5f + 0.5f;
                    labels[p] = std::abs(pan) < 0.025f ? "C" : juce::String(pan < 0 ? "L" : "R") + juce::String(int(std::lround(std::abs(pan) * 100.0f)));
                }
                title = utf8("Pan · Stem ") + juce::String(stem + 1); range = "L100–C–R100"; break;
            case AutomationCurveChoice::fx:
                enabled = song.stemFxAutomation[index].enabled;
                for (size_t p = 0; p < 3; ++p) {
                    values[p] = song.stemFxAutomation[index].active[p] ? 1.0f : 0.0f;
                    labels[p] = song.stemFxAutomation[index].active[p] ? utf8("Ligado") : utf8("Desligado");
                }
                title = utf8("FX ligado/desligado · Stem ") + juce::String(stem + 1); range = "estado em cada ponto"; break;
            case AutomationCurveChoice::filter: {
                enabled = song.stemFilterAutomation[index].enabled;
                constexpr float minimum = 80.0f, maximum = 16000.0f;
                for (size_t p = 0; p < 3; ++p) {
                    const float value = song.stemFilterAutomation[index].values[p];
                    values[p] = value <= 0.0f ? 0.0f : std::clamp(std::log(std::clamp(value, minimum, maximum) / minimum) / std::log(maximum / minimum), 0.0f, 1.0f);
                    labels[p] = value <= 0.0f ? utf8("Padrão") : juce::String(int(std::lround(value))) + " Hz";
                }
                title = utf8("Filtro · corte · Stem ") + juce::String(stem + 1); range = "80–16000 Hz · escala logarítmica"; break;
            }
            case AutomationCurveChoice::delayMix:
                enabled = song.stemDelayMixAutomation[index].enabled;
                for (size_t p = 0; p < 3; ++p) { values[p] = std::clamp(song.stemDelayMixAutomation[index].values[p] / 0.6f, 0.0f, 1.0f); labels[p] = percent(song.stemDelayMixAutomation[index].values[p], 0.6f); }
                title = utf8("Delay · mistura · Stem ") + juce::String(stem + 1); range = "0–60%"; break;
            case AutomationCurveChoice::delayFeedback:
                enabled = song.stemDelayFeedbackAutomation[index].enabled;
                for (size_t p = 0; p < 3; ++p) { values[p] = std::clamp(song.stemDelayFeedbackAutomation[index].values[p] / 0.6f, 0.0f, 1.0f); labels[p] = percent(song.stemDelayFeedbackAutomation[index].values[p], 0.6f); }
                title = utf8("Delay · realimentação · Stem ") + juce::String(stem + 1); range = "0–60%"; break;
            case AutomationCurveChoice::reverbMix:
                enabled = song.stemReverbMixAutomation[index].enabled;
                for (size_t p = 0; p < 3; ++p) { values[p] = std::clamp(song.stemReverbMixAutomation[index].values[p] / 0.6f, 0.0f, 1.0f); labels[p] = percent(song.stemReverbMixAutomation[index].values[p], 0.6f); }
                title = utf8("Reverb · mistura · Stem ") + juce::String(stem + 1); range = "0–60%"; break;
            case AutomationCurveChoice::reverbDecay:
                enabled = song.stemReverbDecayAutomation[index].enabled;
                for (size_t p = 0; p < 3; ++p) {
                    const float value = song.stemReverbDecayAutomation[index].values[p];
                    values[p] = value <= 0.0f ? 0.0f : std::clamp((value - 0.25f) / 2.75f, 0.0f, 1.0f);
                    labels[p] = value <= 0.0f ? utf8("Padrão") : juce::String(value, 2) + " s";
                }
                title = utf8("Reverb · decaimento · Stem ") + juce::String(stem + 1); range = "Padrão ou 0,25–3 s"; break;
            case AutomationCurveChoice::bitcrush: {
                enabled = song.stemBitcrushAutomation[index].enabled;
                for (size_t p = 0; p < 3; ++p) {
                    const float value = song.stemBitcrushAutomation[index].values[p];
                    values[p] = value <= 0.0f ? 0.0f : std::clamp((value - 2.0f) / 14.0f, 0.0f, 1.0f);
                    labels[p] = value <= 0.0f ? utf8("Padrão") : juce::String(int(std::lround(value))) + utf8(" bits");
                }
                title = utf8("Bitcrush · bits · Stem ") + juce::String(stem + 1); range = "Padrão ou 2–16 bits"; break;
            }
        }
        automationEditor.setState(values, enabled, title, range, labels);
    }

    void applyAutomationEditorPoint(int point, float normalized) {
        if (point < 0 || point > 2) return;
        const int stem = std::clamp(automationEditor.selectedStem(), 0, risco::Song::stemCount - 1);
        const auto curve = static_cast<AutomationCurveChoice>(automationEditor.selectedCurve());
        const auto index = static_cast<size_t>(stem);
        const auto n = std::clamp(normalized, 0.0f, 1.0f);
        const auto snap = [](float value, float step) { return std::round(value / step) * step; };
        const auto setEnabled = [this, index](bool value) {
            stemFx[index].setToggleState(song.stemEffect[index] > 0 || song.stemAutomation[index].enabled ||
                song.stemPanAutomation[index].enabled || song.stemFxAutomation[index].enabled || value,
                juce::dontSendNotification);
        };
        switch (curve) {
            case AutomationCurveChoice::volume:
                song.stemAutomation[index].levels[static_cast<size_t>(point)] = snap(n, 0.05f);
                song.stemAutomation[index].enabled = true;
                stemAutoState[index].levels[static_cast<size_t>(point)].store(song.stemAutomation[index].levels[static_cast<size_t>(point)], std::memory_order_release);
                stemAutoState[index].enabled.store(true, std::memory_order_release); setEnabled(true); break;
            case AutomationCurveChoice::pan:
                song.stemPanAutomation[index].levels[static_cast<size_t>(point)] = std::clamp(snap(n * 2.0f - 1.0f, 0.05f), -1.0f, 1.0f);
                song.stemPanAutomation[index].enabled = true;
                stemPanAutoState[index].levels[static_cast<size_t>(point)].store(song.stemPanAutomation[index].levels[static_cast<size_t>(point)], std::memory_order_release);
                stemPanAutoState[index].enabled.store(true, std::memory_order_release); setEnabled(true); break;
            case AutomationCurveChoice::fx:
                song.stemFxAutomation[index].active[static_cast<size_t>(point)] = n >= 0.5f;
                song.stemFxAutomation[index].enabled = true;
                stemFxAutoState[index].active[static_cast<size_t>(point)].store(song.stemFxAutomation[index].active[static_cast<size_t>(point)], std::memory_order_release);
                stemFxAutoState[index].enabled.store(true, std::memory_order_release); setEnabled(true); break;
            case AutomationCurveChoice::filter: {
                constexpr float cutoffs[] = {0,80,160,320,640,1200,2500,5000,8000,12000,16000};
                const float raw = n <= 0.01f ? 0.0f : 80.0f * std::pow(16000.0f / 80.0f, n);
                const auto nearest = std::min_element(std::begin(cutoffs), std::end(cutoffs), [raw](float a, float b) { return std::abs(a - raw) < std::abs(b - raw); });
                song.stemFilterAutomation[index].values[static_cast<size_t>(point)] = *nearest;
                song.stemFilterAutomation[index].enabled = true;
                stemFilterAutoState[index].values[static_cast<size_t>(point)].store(*nearest, std::memory_order_release);
                stemFilterAutoState[index].enabled.store(true, std::memory_order_release); break;
            }
            case AutomationCurveChoice::delayMix:
                song.stemDelayMixAutomation[index].values[static_cast<size_t>(point)] = snap(n * 0.6f, 0.05f);
                song.stemDelayMixAutomation[index].enabled = true;
                stemDelayAutoState[index].mix[static_cast<size_t>(point)].store(song.stemDelayMixAutomation[index].values[static_cast<size_t>(point)], std::memory_order_release);
                stemDelayAutoState[index].mixEnabled.store(true, std::memory_order_release); break;
            case AutomationCurveChoice::delayFeedback:
                song.stemDelayFeedbackAutomation[index].values[static_cast<size_t>(point)] = snap(n * 0.6f, 0.05f);
                song.stemDelayFeedbackAutomation[index].enabled = true;
                stemDelayAutoState[index].feedback[static_cast<size_t>(point)].store(song.stemDelayFeedbackAutomation[index].values[static_cast<size_t>(point)], std::memory_order_release);
                stemDelayAutoState[index].feedbackEnabled.store(true, std::memory_order_release); break;
            case AutomationCurveChoice::reverbMix:
                song.stemReverbMixAutomation[index].values[static_cast<size_t>(point)] = snap(n * 0.6f, 0.05f);
                song.stemReverbMixAutomation[index].enabled = true;
                stemReverbAutoState[index].mix[static_cast<size_t>(point)].store(song.stemReverbMixAutomation[index].values[static_cast<size_t>(point)], std::memory_order_release);
                stemReverbAutoState[index].enabled.store(true, std::memory_order_release); break;
            case AutomationCurveChoice::reverbDecay: {
                constexpr float decay[] = {0,0.25f,0.5f,0.75f,1,1.5f,2,3};
                const auto nearest = std::min_element(std::begin(decay), std::end(decay), [n](float a, float b) { return std::abs(a - (n <= 0.01f ? 0.0f : 0.25f + n * 2.75f)) < std::abs(b - (n <= 0.01f ? 0.0f : 0.25f + n * 2.75f)); });
                song.stemReverbDecayAutomation[index].values[static_cast<size_t>(point)] = *nearest;
                song.stemReverbDecayAutomation[index].enabled = true;
                stemReverbAutoState[index].decay[static_cast<size_t>(point)].store(*nearest, std::memory_order_release);
                stemReverbAutoState[index].decayEnabled.store(true, std::memory_order_release); break;
            }
            case AutomationCurveChoice::bitcrush: {
                constexpr float bits[] = {0,2,3,4,6,8,12,16};
                const float target = n <= 0.01f ? 0.0f : 2.0f + n * 14.0f;
                const auto nearest = std::min_element(std::begin(bits), std::end(bits), [target](float a, float b) { return std::abs(a - target) < std::abs(b - target); });
                song.stemBitcrushAutomation[index].values[static_cast<size_t>(point)] = *nearest;
                song.stemBitcrushAutomation[index].enabled = true;
                stemBitcrushAutoState[index].bits[static_cast<size_t>(point)].store(*nearest, std::memory_order_release);
                stemBitcrushAutoState[index].enabled.store(true, std::memory_order_release); break;
            }
        }
        status.setText(utf8("Curva do Stem ") + juce::String(stem + 1) + utf8(" · ponto ") +
            juce::String(point == 0 ? "início" : point == 1 ? "meio" : "fim") + utf8(" editado."), juce::dontSendNotification);
        refreshAutomationEditor();
    }

    void setAutomationEditorEnabled(bool enabled) {
        const int stem = std::clamp(automationEditor.selectedStem(), 0, risco::Song::stemCount - 1);
        const auto curve = static_cast<AutomationCurveChoice>(automationEditor.selectedCurve());
        const auto index = static_cast<size_t>(stem);
        switch (curve) {
            case AutomationCurveChoice::volume: song.stemAutomation[index].enabled = enabled; stemAutoState[index].enabled.store(enabled, std::memory_order_release); break;
            case AutomationCurveChoice::pan: song.stemPanAutomation[index].enabled = enabled; stemPanAutoState[index].enabled.store(enabled, std::memory_order_release); break;
            case AutomationCurveChoice::fx: song.stemFxAutomation[index].enabled = enabled; stemFxAutoState[index].enabled.store(enabled, std::memory_order_release); break;
            case AutomationCurveChoice::filter: song.stemFilterAutomation[index].enabled = enabled; stemFilterAutoState[index].enabled.store(enabled, std::memory_order_release); break;
            case AutomationCurveChoice::delayMix: song.stemDelayMixAutomation[index].enabled = enabled; stemDelayAutoState[index].mixEnabled.store(enabled, std::memory_order_release); break;
            case AutomationCurveChoice::delayFeedback: song.stemDelayFeedbackAutomation[index].enabled = enabled; stemDelayAutoState[index].feedbackEnabled.store(enabled, std::memory_order_release); break;
            case AutomationCurveChoice::reverbMix: song.stemReverbMixAutomation[index].enabled = enabled; stemReverbAutoState[index].enabled.store(enabled, std::memory_order_release); break;
            case AutomationCurveChoice::reverbDecay: song.stemReverbDecayAutomation[index].enabled = enabled; stemReverbAutoState[index].decayEnabled.store(enabled, std::memory_order_release); break;
            case AutomationCurveChoice::bitcrush: song.stemBitcrushAutomation[index].enabled = enabled; stemBitcrushAutoState[index].enabled.store(enabled, std::memory_order_release); break;
        }
        status.setText(utf8("Curva do Stem ") + juce::String(stem + 1) + (enabled ? utf8(" · ligada.") : utf8(" · desligada; pontos conservados.")), juce::dontSendNotification);
        refreshAutomationEditor();
    }

    static void configureTogglePalette(juce::TextButton& button,
                                       juce::Colour off, juce::Colour on) {
        button.setColour(juce::TextButton::buttonColourId, off);
        button.setColour(juce::TextButton::buttonOnColourId, on);
        button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd4dce5));
        button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff5fff8));
    }

    static void setToggleLabel(juce::TextButton& button, bool enabled,
                               const juce::String& on, const juce::String& off) {
        const auto& desired = enabled ? on : off;
        if (button.getButtonText() != desired) button.setButtonText(desired);
    }

    void refreshToggleDisplays() {
        const bool isPlaying = !seedTrackPreview && playing.load(std::memory_order_acquire);
        const bool hearsNotes = isPlaying && !directEnabled.load(std::memory_order_acquire);
        const bool hearsGrid = hearsNotes && activeSong.load(std::memory_order_acquire) != nullptr;
        const bool hearsCell = hearsGrid && previewingCell;
        const bool hearsInput = hearsNotes && active.load(std::memory_order_acquire) != nullptr;
        playSongButton.setToggleState(hearsGrid && !hearsCell, juce::dontSendNotification);
        previewCellButton.setToggleState(hearsCell, juce::dontSendNotification);
        play.setToggleState(hearsInput, juce::dontSendNotification);
        setToggleLabel(playSongButton, hearsGrid && !hearsCell,
            uiText("Música ativa", "Song playing", "Morceau en cours", "Pieza activa"),
            uiText("Tocar música", "Play song", "Jouer morceau", "Tocar pieza"));
        setToggleLabel(previewCellButton, hearsCell,
            uiText("Ouvindo célula", "Cell playing", "Cellule en cours", "Célula activa"),
            uiText("Ouvir célula", "Play cell", "Écouter cellule", "Oír célula"));
        setToggleLabel(play, hearsInput,
            uiText("Ouvindo texto/desenho", "Input playing", "Entrée en cours", "Entrada activa"),
            uiText("Ouvir texto/desenho", "Play text/drawing", "Écouter texte/dessin", "Oír texto/dibujo"));
        setToggleLabel(functionMode, functionMode.getToggleState(),
            uiText("Função 8b: ON", "8-bit fn: ON", "Fonct. 8b : ON", "Func. 8b: ON"),
            uiText("Função 8b: OFF", "8-bit fn: OFF", "Fonct. 8b : OFF", "Func. 8b: OFF"));
        setToggleLabel(loopMode, loopMode.getToggleState(), utf8("Loop: ON"), utf8("Loop: OFF"));
        setToggleLabel(autoMode, autoMode.getToggleState(),
            uiText("Fórmula auto: ON", "Auto formula: ON", "Formule auto : ON", "Fórmula auto: ON"),
            uiText("Fórmula auto", "Auto formula", "Formule auto", "Fórmula auto"));
        setToggleLabel(directMode, directMode.getToggleState(),
            uiText("Fórmula dir.: ON", "Direct formula: ON", "Formule dir. : ON", "Fórmula dir.: ON"),
            uiText("Fórmula dir.: OFF", "Play formula", "Écouter formule", "Oír fórmula"));
        setToggleLabel(recButton, recButton.getToggleState(), utf8("REC ON"), utf8("REC OFF"));
        setToggleLabel(autoPhraseButton, autoPhraseButton.getToggleState(),
            uiText("Frases: ON", "Phrases: ON", "Phrases : ON", "Frases: ON"),
            uiText("Frases auto", "Auto phrases", "Phrases auto", "Frases auto"));
        setToggleLabel(loopRangeButton, loopRangeButton.getToggleState(),
            uiText("Trecho: ON", "Range: ON", "Plage : ON", "Tramo: ON"),
            uiText("Trecho: OFF", "Range: OFF", "Plage : OFF", "Tramo: OFF"));
    }

    static std::string normalizeFormulaText(std::string value) {
        value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }), value.end());
        return value;
    }

    void refreshFormulaGridStatus() {
        const auto current = normalizeFormulaText(formulaInput.getText().toStdString());
        bool inBank = false;
        int placements = 0;
        if (!current.empty()) {
            for (int i = 0; i < song.cellCount; ++i) {
                const auto& cell = song.bank[static_cast<size_t>(i)];
                if (!cell.bytebeatFormula || normalizeFormulaText(cell.formulaExpression) != current)
                    continue;
                inBank = true;
                for (int p = 0; p < song.placementCount; ++p)
                    if (song.grid[static_cast<size_t>(p)].cellId == cell.id) ++placements;
            }
        }

        juce::String label;
        juce::Colour colour(0xff526171);
        if (current.empty()) {
            label = utf8("EDITOR · VAZIO");
        } else if (placements > 0) {
            label = uiText("NA GRADE ×", "ON GRID ×", "DANS LA GRILLE ×", "EN CUADRÍCULA ×")
                + juce::String(placements);
            colour = juce::Colour(0xff16776c);
        } else if (inBank) {
            label = uiText("NO BANCO · INSERIR NA GRADE", "IN BANK · PLACE ON GRID",
                           "EN BANQUE · PLACER EN GRILLE", "EN BANCO · INSERTAR EN CUADRÍCULA");
            colour = juce::Colour(0xff496bb8);
        } else if (normalizeFormulaText(compiledFormulaExpression) == current) {
            label = recButton.getToggleState()
                ? uiText("COMPILADA · REC ON: RECOMPILAR", "COMPILED · REC ON: RECOMPILE", "COMPILÉE · REC ON : RECOMPILER", "COMPILADA · REC ON: RECOMPILAR")
                : uiText("COMPILADA · ATIVE REC E RECOMPILAR", "COMPILED · ENABLE REC AND RECOMPILE", "COMPILÉE · ACTIVER REC ET RECOMPILER", "COMPILADA · ACTIVE REC Y RECOMPILE");
            colour = juce::Colour(0xff7656bd);
        } else {
            label = utf8("SÓ NO EDITOR · COMPILAR");
        }
        if (formulaGridStatus.getText() != label)
            formulaGridStatus.setText(label, juce::dontSendNotification);
        if (formulaGridStatus.findColour(juce::Label::backgroundColourId) != colour)
            formulaGridStatus.setColour(juce::Label::backgroundColourId, colour);
    }

    std::pair<juce::String, juce::Colour> currentAudioIndicator() const {
        if (seedTrackPreview)
            return {seedTrackPreviewPlaying.load(std::memory_order_relaxed)
                        ? utf8("TOCANDO PRÉVIA DA FAIXA · seed ") + juce::String(seedTrackPreview->song->compositionSeed)
                        : utf8("PRÉVIA DA FAIXA · parada"), juce::Colour(0xff56b8cf)};
        if (!playing.load(std::memory_order_acquire))
            return {uiText("PARADO · nenhuma fonte está tocando", "STOPPED · no source playing", "ARRÊT · aucune source en cours", "PARADO · ninguna fuente activa"), juce::Colour(0xff697583)};

        if (directEnabled.load(std::memory_order_acquire)) {
            return {utf8(compiledFormulaExpression.rfind("f:", 0) == 0 ? "TOCANDO · FLOATBEAT DIRETO · " : "TOCANDO · FÓRMULA BYTEBEAT DIRETA · ") +
                    juce::String::fromUTF8(compiledFormulaExpression.c_str()),
                    juce::Colour(0xffad84f7)};
        }

        if (const auto* playback = activeSong.load(std::memory_order_acquire)) {
            const double seconds = playheadSeconds.load(std::memory_order_relaxed);
            juce::String names;
            int activeCount = 0;
            for (int i = 0; i < playback->count; ++i) {
                const auto& item = playback->placements[static_cast<size_t>(i)];
                if (seconds < item.start || seconds >= item.start + item.playLength) continue;
                const auto* cell = risco::findCell(song, item.cellId);
                juce::String type = item.bytebeatFormula ? utf8("FÓRMULA") : utf8("DESENHO");
                if (cell != nullptr && !item.bytebeatFormula) {
                    if (cell->generatedPhrase) type = utf8("GERAÇÃO");
                    else if (!cell->inscription.ascii.empty() && cell->inscription.count > 0)
                        type = utf8("TEXTO + DESENHO");
                    else if (!cell->inscription.ascii.empty()) type = utf8("TEXTO");
                }
                if (activeCount < 2) {
                    if (names.isNotEmpty()) names += utf8(" + ");
                    names += utf8("STEM ") + juce::String(item.stem + 1) + utf8(" · ") + type + utf8(" · ") +
                        (cell != nullptr ? juce::String::fromUTF8(cell->name.data(), int(cell->name.size()))
                                         : utf8("célula"));
                }
                ++activeCount;
            }
            if (previewingCell) {
                const juce::String detail = names.isNotEmpty() ? names : utf8("célula selecionada");
                return {utf8("TOCANDO PRÉVIA DO BANCO · ") + detail + utf8(" · FORA DA GRADE"),
                        juce::Colour(0xff56b8cf)};
            }
            if (activeCount == 0)
                return {utf8("TOCANDO A GRADE · intervalo sem célula"), juce::Colour(0xffffbf55)};
            return {utf8("TOCANDO A GRADE · ") + names +
                    (activeCount > 2 ? utf8(" + ") + juce::String(activeCount - 2) + utf8(" outras") : juce::String()),
                    juce::Colour(0xffffbf55)};
        }

        if (active.load(std::memory_order_acquire) != nullptr) {
            if (currentPhraseGenerated)
                return {utf8("TOCANDO AO VIVO · FRASE GENERATIVA · seed ") +
                        juce::String(currentPhraseSeed) + utf8(" · variação ") +
                        juce::String(currentPhraseVariation + 1), juce::Colour(0xff56d6ad)};
            const auto input = activeInput();
            const bool hasText = !input.ascii.empty();
            const bool hasDrawing = input.count > 0;
            juce::String type = hasText && hasDrawing ? utf8("TEXTO + DESENHO") :
                                hasText ? utf8("TEXTO") : hasDrawing ? utf8("DESENHO") :
                                          utf8("ENTRADA AO VIVO");
            if (formulaEnabled.load(std::memory_order_acquire)) type += utf8(" + FUNÇÃO 8 BITS");
            return {utf8("TOCANDO AO VIVO · ") + type + utf8(" · FORA DA GRADE"),
                    juce::Colour(0xff56d6ad)};
        }
        return {utf8("SEM FONTE DE ÁUDIO ATIVA"), juce::Colour(0xffb85e66)};
    }

    std::array<GridState, 32> gridUndoHistory{};
    std::vector<std::unique_ptr<risco::Cell>> bankUndo, bankRedo;
    int gridUndoCount = 0;
    GridState placementDragBefore;
    risco::Inscription inscription;
    std::array<risco::Inscription, 32> drawingUndoHistory{}, drawingRedoHistory{};
    int drawingUndoCount = 0, drawingRedoCount = 0;
    risco::Song song;
    risco::FormulaLibrary formulaLibrary;
    bool localLibraryWritable = true;
    std::vector<risco::FormulaRecord> catalogCandidates;
    juce::TextEditor ascii, formulaInput, seedInput, cellName, startBeat;
    bool manualGlitchMode = false;
    std::string seedBeforeGlitch;
    juce::Label formulaGridStatus;
    juce::TextButton play, cross, clear, undoDrawingButton, redoDrawingButton, glyph, functionMode, loopMode, applyFormula, stopButton;
    juce::TextButton directMode, resetDirectButton;
    juce::TextButton formulaToNotesButton;
    juce::TextButton saveFormulaNotesButton;
    juce::TextButton restartSongButton, loopRangeButton;
    juce::TextButton zoomOutButton, zoomInButton, zoomFitButton, undoGridButton;
    std::array<juce::Slider, risco::Song::stemCount> stemPan;
    std::array<juce::Slider, risco::Song::stemCount> stemGain;
    std::array<juce::TextButton, risco::Song::stemCount> stemFx;
    std::array<risco::StemEffect, risco::Song::stemCount> stemEffects;
    struct AutomationState {
        std::atomic<bool> enabled{false};
        std::array<std::atomic<float>, 3> levels{{1.0f,1.0f,1.0f}};
    };
    std::array<AutomationState, risco::Song::stemCount> stemAutoState;
    std::array<risco::StemAutomation, risco::Song::stemCount> stemAutoProcessors;
    struct PanAutomationState {
        std::atomic<bool> enabled{false};
        std::array<std::atomic<float>,3> levels{{0,0,0}};
    };
    struct FxAutomationState {
        std::atomic<bool> enabled{false};
        std::array<std::atomic<bool>,3> active{{true,true,true}};
    };
    struct FilterAutomationState {
        std::atomic<bool> enabled{false};
        std::array<std::atomic<float>,3> values{{1200,1200,1200}};
    };
    struct DelayAutomationState {
        std::atomic<bool> mixEnabled{false}, feedbackEnabled{false};
        std::array<std::atomic<float>,3> mix{{0.3f,0.3f,0.3f}};
        std::array<std::atomic<float>,3> feedback{{0.35f,0.35f,0.35f}};
    };
    struct ReverbAutomationState {
        std::atomic<bool> enabled{false};
        std::atomic<bool> decayEnabled{false};
        std::array<std::atomic<float>,3> mix{{0.25f,0.25f,0.25f}};
        std::array<std::atomic<float>,3> decay{{0.0f,0.0f,0.0f}};
    };
    struct BitcrushAutomationState {
        std::atomic<bool> enabled{false};
        std::array<std::atomic<float>,3> bits{{0,0,0}};
    };
    std::array<PanAutomationState, risco::Song::stemCount> stemPanAutoState;
    std::array<FxAutomationState, risco::Song::stemCount> stemFxAutoState;
    std::array<FilterAutomationState, risco::Song::stemCount> stemFilterAutoState;
    std::array<DelayAutomationState, risco::Song::stemCount> stemDelayAutoState;
    std::array<ReverbAutomationState, risco::Song::stemCount> stemReverbAutoState;
    std::array<BitcrushAutomationState, risco::Song::stemCount> stemBitcrushAutoState;
    std::array<risco::StemPanAutomation, risco::Song::stemCount> stemPanAutoProcessors;
    std::array<std::atomic<int>, risco::Song::stemCount> stemEffectValue{};
    std::array<std::atomic<float>, risco::Song::stemCount> stemFilterCutoffValue{};
    std::array<std::atomic<float>, risco::Song::stemCount> stemReverbMixValue{{0.25f, 0.25f, 0.25f, 0.25f, 0.25f}};
    std::array<std::atomic<float>, risco::Song::stemCount> stemReverbDecayValue{};
    std::array<std::atomic<int>, risco::Song::stemCount> stemBitcrushBitsValue{};
    std::array<std::atomic<float>, risco::Song::stemCount> stemDelayMixValue{{0.3f, 0.3f, 0.3f, 0.3f, 0.3f}};
    std::array<std::atomic<float>, risco::Song::stemCount> stemDelayFeedbackValue{{0.35f, 0.35f, 0.35f, 0.35f, 0.35f}};
    std::array<juce::TextButton, risco::Song::stemCount> stemMute, stemSolo;
    std::array<juce::Label, risco::Song::stemCount> stemPanLabel;
    std::array<juce::ComboBox, risco::Song::stemCount> stemRoleSelect;
    juce::TextButton generateFormulaButton, autoMode;
    juce::TextButton reuseFormulaButton, importCatalogButton;
    juce::TextButton varyPhraseButton, autoPhraseButton;
    juce::TextButton recButton;
    juce::ComboBox seedSource;
    juce::ComboBox repositorySelect;
    juce::String bankQuery;
    bool bankFavoritesOnly = false;
    int bankSelectionBeforeMenu = -1;
    std::unique_ptr<juce::AlertWindow> bankSearchDialog;
    std::unique_ptr<juce::AlertWindow> libraryDialog;
    std::unique_ptr<juce::AlertWindow> libraryGroupNameDialog;
    std::shared_ptr<ProjectLibraryResult> libraryResult;
    juce::ThreadPool libraryPool{1};
    std::vector<ProjectLibraryEntry> libraryIndex;
    juce::StringArray librarySourcePaths;
    std::vector<ProjectLibraryGroup> libraryGroups;
    bool libraryCatalogSaveFailed = false;
    std::vector<juce::File> libraryPendingFiles;
    std::vector<juce::String> libraryReadErrors;
    juce::File libraryActiveFile;
    std::shared_ptr<risco::Song> libraryCandidateSong;
    int libraryCandidateCell = -1;
    std::string libraryCandidateDescription;
    juce::ComboBox stemSelect, roleSelect;
        juce::TextButton storeCellButton, updateCellButton, placeCellButton, playSongButton;
    AutoSettingsButton autoRouteButton;
    juce::TextButton previewCellButton, seedTrackButton;
    juce::TextButton saveButton, openButton, removeLastButton, albumMenuButton;
    juce::TextButton automationEditorButton;
    AutomationEditor automationEditor;
    TemporalGestureEditor gestureEditor;
    juce::ComboBox cellSpan, repetitions, bankSelect, bitDepthSelect, timbreSelect, shapeSelect, meterSelect;
    juce::TextButton ledSourceButton;
    juce::ComboBox fontSelect;
    juce::ComboBox formulaScaleSelect, formulaTonicSelect, formulaMotionSelect, responseModeSelect;
    int formulaProgressionChoice = 0, formulaHarmonicBarsChoice = 1;
    double formulaSwingChoice = 0.0;
    int formulaSubdivisionChoice = 0;
    bool formulaSyncopationChoice = false;
    int formulaPulsesChoice = 0;
    int inputInterpretationChoice = 0;
    float inputInterpretationStrength = 1;
    risco::TemporalGestureCurve inputGestureCurve;
    int inputGestureRangeSemitones = 12;
    std::uint64_t liveInputSeed = 0;
    std::uint32_t liveDerivationIndex = 0;
    std::uint32_t liveCompletedLoops = 0;
    int autoEvolutionStrength = 2;
    int autoEvolutionEveryLoops = 1;
    double liveObservedPlayhead = 0.0;
    std::int64_t liveObservedCycle = 0;
    std::array<int, 4> formulaChordRootChoices{{-1, -1, -1, -1}};
    juce::ComboBox formulaGroupingSelect;
    juce::ComboBox languageSelect;
    juce::TextButton aboutButton, tutorialButton, learnButton;
    HelpOverlay helpOverlay;
    juce::Slider shapeDriveSlider;
    juce::Label status, activityStatus, generationStatus;
    juce::String activityMessage;
    std::string compiledFormulaExpression;
    juce::Label masterLabel, bpmLabel, shapeDriveLabel;
    juce::Slider masterSlider, bpmSlider;
    std::vector<std::unique_ptr<risco::Sequence>> snapshots;
    std::vector<std::unique_ptr<risco::SongPlayback>> songSnapshots;
    std::atomic<const risco::SongPlayback*> liveInputPlayback{nullptr};
    std::vector<std::unique_ptr<risco::BytebeatProgram>> formulaSnapshots;
    std::vector<CompositionVersion> compositionVersions;
    int activeCompositionVersion = -1;
    std::unique_ptr<juce::AlertWindow> compositionSeedDialog, compositionSeedPreviewDialog;
    std::unique_ptr<juce::AlertWindow> seedTrackDialog, seedTrackReviewDialog, seedTrackPartDialog;
    struct SeedTrackPreview {
        SeedTrackPreview(std::shared_ptr<const risco::Song> source,
                         std::unique_ptr<risco::SongPlayback> prepared, double rate)
            : song(std::move(source)), playback(std::move(prepared)), renderer(*song, *playback, -1, false, rate),
              rateHz(rate), endFrame(std::uint32_t(std::ceil((playback->end +
                  risco::SongRender::tailSecondsFor(*song)) * rateHz)) + renderer.latencySamples()) {}

        std::shared_ptr<const risco::Song> song;
        std::unique_ptr<const risco::SongPlayback> playback;
        risco::SongRender renderer;
        double rateHz;
        std::uint32_t endFrame;
        std::uint32_t frame = 0;
    };
    std::unique_ptr<SeedTrackPreview> seedTrackPreview;
    std::atomic<bool> seedTrackPreviewPlaying{false};
    std::unique_ptr<juce::AlertWindow> albumDialog, albumReviewDialog;
    std::unique_ptr<risco::AlbumManifest> pendingAlbumManifest;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<SongExport> exportJob;
    std::unique_ptr<AlbumGenerationJob> albumJob;
    std::unique_ptr<AlbumRenderJob> albumRenderJob;
    int exportBitDepth = 16;
    bool exportPreMaster = false;
    juce::File currentFile;
    juce::File lastAlbumDirectory;
    std::shared_ptr<RecoveryState> recoveryState = std::make_shared<RecoveryState>();
    juce::ThreadPool recoveryPool{1};
    bool recoveryPending = recoveryFile().existsAsFile();
    std::uint32_t lastRecoveryMillis = juce::Time::getMillisecondCounter();
    std::atomic<const risco::Sequence*> active{nullptr};
    std::atomic<const risco::SongPlayback*> activeSong{nullptr};
    std::atomic<unsigned> audioReaders{0};
    std::atomic<const risco::BytebeatProgram*> activeFormula{nullptr};
    std::atomic<bool> playing{false}, restart{false};
    std::atomic<bool> formulaEnabled{false}, loopEnabled{false};
    std::atomic<bool> loopRangeEnabled{false};
    std::atomic<double> loopRangeStartSeconds{0.0}, loopRangeEndSeconds{0.0};
    std::atomic<bool> directEnabled{false}, resetDirect{false};
    std::atomic<float> masterGain{0.35f};
    std::atomic<double> renderingBpm{120.0};
    std::array<std::atomic<float>, risco::Song::stemCount> stemPanValue{};
    std::array<std::atomic<float>, risco::Song::stemCount> stemGainValue{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<std::atomic<bool>, risco::Song::stemCount> stemMuted{}, stemSoloed{};
    std::atomic<int> renderingBits{8};
    std::atomic<int> renderingTimbre{0};
    std::array<risco::HistoricalAyEngine, risco::Song::stemCount> historicalStemEngines;
    std::array<risco::HistoricalAyEngine, risco::Song::stemCount> liveHistoricalStemEngines;
    risco::HistoricalAyEngine historicalPreviewEngine;
    int historicalPreparedTimbre = 0; // Audio-thread state only.
    std::atomic<int> renderingAyNoisePeriod{16};
    std::atomic<int> renderingAyEnvelopeMode{0};
    std::atomic<float> renderingAyEnvelopeRamp{0.5f};
    std::atomic<int> renderingHistoricalAyTexture{0};
    std::atomic<int> renderingHistoricalAyNoisePeriod{16};
    std::atomic<int> renderingHistoricalAyEnvelope{0};
    std::atomic<float> renderingHistoricalAyEnvelopeRamp{0.5f};
    std::atomic<int> renderingShape{0};
    std::atomic<float> renderingDrive{1.0f};
    std::atomic<std::uint32_t> displayedDirectTime{0};
    std::atomic<double> playheadSeconds{0.0}, liveDuration{1.0};
    std::array<std::atomic<float>, 2048> scope{};
    std::atomic<std::uint64_t> publishedScopeWriteIndex{0};
    std::atomic<float> outputTruePeak{0.0f}, outputTruePeakHold{0.0f}, outputReductionDb{0.0f};
    std::atomic<float> outputRms{0.0f}, outputCorrelation{0.0f}, outputMonoRms{0.0f};
    std::atomic<bool> outputClipLatched{false};
    std::atomic<float> outputLufsM{-70}, outputLufsS{-70}, outputLufsI{-70};
    std::atomic<bool> outputLufsMReady{false}, outputLufsSReady{false}, outputLufsIReady{false};
    std::atomic<bool> resetOutputLoudness{false};
    double sampleRate = 48000.0;
    risco::OutputStage outputStage;
    unsigned long long sampleIndex = 0;
    std::uint64_t directSampleIndex = 0, scopeWriteIndex = 0;
    std::uint32_t directLastTime = 0;
    float directLastValue = 0.0f;
    bool directHasValue = false;
    std::uint64_t generationIndex = 0, lastSeed = 0;
    std::uint32_t nextAutoTick = 64000;
    int lastSeedSource = 0;
    unsigned phraseVariation = 0;
    std::uint64_t currentPhraseSeed = 0;
    std::uint32_t currentPhraseVariation = 0;
    std::uint32_t currentPhraseVersion = 2;
    std::uint8_t currentPhraseSectionMask = 0x0f;
    risco::SectionEdits currentSectionEdits{};
    risco::PhraseMeters currentPhraseMeters{};
    risco::PhraseSectionBars currentPhraseSectionBars{{1,1,1,1}};
    bool compareOriginalSections = false;
    std::uint32_t selectedPhraseVersion = 2;
    bool currentPhraseGenerated = false;
    std::uint32_t nextPhraseMillis = 0;
    std::uint32_t marqueeScrollStartMillis = 0;
    bool hasLastSeed = false;
    bool drawing = false;
    std::uint32_t drawingStartMillis = 0;
    std::uint32_t lastLiveDrawingUpdateMillis = 0;
    int releasedStrokeIndex = -1;
    float releasedStrokeShift = 0.0f;
    std::uint32_t releasedStrokeMillis = 0;
    bool ledShowsDrawing = false;
    bool textEntryMode = false;
    bool previewingCell = false;
    int selectedPlacement = -1;
    double gridScrollBeat = 0.0;
    double loopRangeStartBeat = 0.0, loopRangeEndBeat = 0.0, loopRangeAnchorBeat = 0.0;
    bool loopRangeSelecting = false;
    int gridZoomBeats = 0;
    int draggedPlacement = -1;
    int movingStroke = -1;
    bool movingStrokeHistoryPushed = false;
    juce::Point<int> lastStrokeMovePoint;
    GridDragMode gridDragMode = GridDragMode::none;
    int placementDragOriginalBeat = 0;
    int placementDragBeat = 0;
    int placementDragOriginalStem = 0;
    int placementDragStem = 0;
    int placementDragOriginalLength = 1;
    int placementDragLength = 1;
    int placementDragOriginalSource = 0;
    int placementDragSource = 0;
    int placementDragGrabOffset = 0;
    bool placementDragMoved = false;
    int pendingStem = 0;
    RiscoRetroLookAndFeel retroLookAndFeel;
    juce::TooltipWindow tooltipWindow{this, 500};
};

class RiscoApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Risco"; }
    const juce::String getApplicationVersion() override { return "0.1.0-prototype"; }
    void initialise(const juce::String& arguments) override {
        regularMono = juce::Typeface::createSystemTypefaceFor(
            BinaryData::DejaVuSansMono_ttf, BinaryData::DejaVuSansMono_ttfSize);
        boldMono = juce::Typeface::createSystemTypefaceFor(
            BinaryData::DejaVuSansMonoBold_ttf, BinaryData::DejaVuSansMonoBold_ttfSize);
        const auto args = juce::StringArray::fromTokens(arguments, true);
        if (args.size() == 1 && args[0] == "--check-recovery") {
            setApplicationReturnValue(risco::diagnostics::checkRecovery());
            quit();
            return;
        }
        // Offscreen help preview: no main window, transport or audio device.
        if (args.size() >= 2 && args[0] == "--render-help") {
            RiscoRetroLookAndFeel look;
            HelpOverlay preview;
            preview.setLookAndFeel(&look);
            preview.setSize(1280, 900);
            preview.setLanguage(args.size() > 3 ? args[3].getIntValue() : 1);
            preview.setTopic(args.size() > 2 ? args[2].getIntValue() : 1);
            auto stream = juce::File(args[1]).createOutputStream();
            if (stream) {
                stream->setPosition(0); stream->truncate();
                juce::PNGImageFormat png;
                const auto image = preview.createComponentSnapshot(preview.getLocalBounds());
                setApplicationReturnValue(png.writeImageToStream(image, *stream) ? 0 : 1);
            } else setApplicationReturnValue(1);
            preview.setLookAndFeel(nullptr);
            quit();
            return;
        }
        window = std::make_unique<MainWindow>();
    }
    void shutdown() override { window.reset(); }
private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        MainWindow() : DocumentWindow("Risco", juce::Colour(0xff0b1220), allButtons) {
            setUsingNativeTitleBar(true);
            setIcon(juce::ImageFileFormat::loadFrom(BinaryData::icon48_png,
                                                   BinaryData::icon48_pngSize));
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(1024, 720, 3840, 2160);
            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            if (display != nullptr) setBounds(display->userBounds.toNearestInt());
            else centreWithSize(1280, 900);
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<MainWindow> window;
    juce::Typeface::Ptr regularMono, boldMono;
};
}
START_JUCE_APPLICATION(RiscoApplication)
