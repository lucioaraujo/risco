// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <vector>

// Original implementation for Risco. Visual references: Aquorbium LearnPanel
// and Asterismus quick/how/listen cards; no sibling project code dependency.
class HelpReader final : public juce::Component, private juce::ListBoxModel {
    static juce::String u(const char* s) { return juce::String::fromUTF8(s); }
    struct Page { juce::String title, body, practice; };
    class Card final : public juce::Component {
    public:
        Page page;
        juce::Colour accent{0xff65d9e8};
        juce::String label;
        void resized() override { layout(); }
        void layout() {
            const float width = float(std::max(100, getWidth() - 56));
            auto build = [width](juce::TextLayout& target, const juce::String& value,
                                 float size, juce::Colour colour, bool bold) {
                juce::AttributedString text;
                text.setLineSpacing(5.0f);
                text.append(value, juce::Font(juce::FontOptions("DejaVu Sans Mono", size,
                    bold ? juce::Font::bold : juce::Font::plain)), colour);
                target.createLayout(text, width);
            };
            build(title, page.title, 23, accent, true);
            build(body, page.body, 16, juce::Colour(0xffd6e9f6), false);
            build(practice, label + "\n\n" + page.practice, 16, juce::Colour(0xffffd59a), false);
            const int needed = int(title.getHeight() + body.getHeight() + practice.getHeight()) + 164;
            if (getHeight() != needed) setSize(getWidth(), needed);
            repaint();
        }
        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colour(0xff101e31));
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 10);
            g.setColour(accent.withAlpha(0.6f));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 10, 1);
            const float w = float(getWidth() - 56);
            title.draw(g, {28, 26, w, title.getHeight()});
            const float y = 46 + title.getHeight();
            body.draw(g, {28, y, w, body.getHeight()});
            const float py = y + body.getHeight() + 26;
            g.setColour(juce::Colour(0xff342b20));
            g.fillRoundedRectangle(juce::Rectangle<float>(16, py, float(getWidth() - 32),
                practice.getHeight() + 32), 8);
            practice.draw(g, {28, py + 16, w, practice.getHeight()});
        }
    private:
        juce::TextLayout title, body, practice;
    };
public:
    HelpReader() {
        addAndMakeVisible(index);
        index.setModel(this);
        index.setRowHeight(52);
        index.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff0b1523));
        addAndMakeVisible(view);
        view.setViewedComponent(&card, false);
        view.setScrollBarsShown(true, false);
        for (auto* button : {&previous, &next}) addAndMakeVisible(button);
        addAndMakeVisible(position);
        previous.onClick = [this] { index.selectRow(selected - 1); };
        next.onClick = [this] { index.selectRow(selected + 1); };
    }
    ~HelpReader() override { index.setModel(nullptr); view.setViewedComponent(nullptr); }
    void setDocument(const juce::String& document, int lang, int topic) {
        currentTopic = std::clamp(topic, 0, 2);
        language = std::clamp(lang, 0, 3);
        pages.clear();
        const char* practiceText[] = {
            "Mude um controle por vez. Compare o resultado e volte ao valor anterior antes de continuar.",
            "Change one control at a time. Compare the result, then restore the previous value before continuing.",
            "Changez une commande à la fois. Comparez le résultat, puis rétablissez la valeur précédente.",
            "Cambie un control a la vez. Compare el resultado y restaure el valor anterior antes de continuar."};
        juce::String remaining = document;
        while (remaining.isNotEmpty()) {
            const int boundary = remaining.indexOf("\n\n");
            auto paragraph = (boundary < 0 ? remaining : remaining.substring(0, boundary)).trim();
            remaining = boundary < 0 ? juce::String() : remaining.substring(boundary + 2);
            if (paragraph.isEmpty()) continue;
            const int divider = paragraph.indexOf(u(" — "));
            const int dot = paragraph.indexOf(". ");
            const int line = paragraph.indexOfChar('\n');
            int cut = divider >= 0 ? divider : dot >= 0 && dot < 90 ? dot : line >= 0 ? line : -1;
            auto title = cut >= 0 ? paragraph.substring(0, cut) : paragraph.substring(0, 56);
            auto body = cut >= 0 ? paragraph.substring(cut + (divider >= 0 ? 3 : dot >= 0 ? 2 : 1)).trim() : paragraph;
            if (body.isEmpty()) continue;
            pages.push_back({title, body, u(practiceText[language])});
        }
        if (topic == 2)
            for (auto& page : pages) page.practice = exercise(page.title, page.practice);
        if (topic == 1) tutorial();
        const char* back[] = {"Anterior", "Previous", "Précédent", "Anterior"};
        const char* forward[] = {"Próximo", "Next", "Suivant", "Siguiente"};
        previous.setButtonText(u(back[language])); next.setButtonText(u(forward[language]));
        const int restorePage = rememberedPage[static_cast<size_t>(currentTopic)];
        selected = -1;
        index.updateContent(); index.deselectAllRows();
        index.selectRow(std::clamp(restorePage, 0, std::max(0, getNumRows() - 1)));
        resized();
    }
    void resized() override {
        if (getWidth() < 1 || getHeight() < 1) return;
        auto bounds = getLocalBounds();
        const auto footer = bounds.removeFromBottom(40);
        previous.setBounds(footer.getX(), footer.getY() + 5, 125, 30);
        next.setBounds(footer.getRight() - 125, footer.getY() + 5, 125, 30);
        position.setBounds(footer.getX() + 135, footer.getY() + 5, footer.getWidth() - 270, 30);
        position.setJustificationType(juce::Justification::centred);
        index.setBounds(bounds.removeFromLeft(std::clamp(getWidth() / 4, 190, 260)));
        bounds.removeFromLeft(18);
        view.setBounds(bounds);
        card.setSize(std::max(100, view.getWidth() - 16), card.getHeight());
        card.layout();
    }
private:
    juce::String exercise(const juce::String& title, const juce::String& fallback) const {
        struct Exercise { const char* headings; const char* text[4]; };
        static const Exercise exercises[] = {
            {"GUIAR ACORDE|CHORD GUIDE|GUIDAGE D’ACCORD|GUÍA DE ACORDE", {
                "Com t^(t>>7), compare Acompanhar e Guiar acorde na mesma escala e Tom. Ouça os apoios da melodia; mantenha BPM e MASTER constantes.",
                "With t^(t>>7), compare Acompanhar and Guiar acorde in the same scale and Tom. Listen to melodic anchors with BPM and MASTER unchanged.",
                "Avec t^(t>>7), comparez Acompanhar et Guiar acorde avec la même gamme et le même Tom. Écoutez les appuis mélodiques sans changer BPM ou MASTER.",
                "Con t^(t>>7), compare Acompanhar y Guiar acorde con la misma escala y Tom. Escuche los apoyos melódicos manteniendo BPM y MASTER."}},
            {"BYTEBEAT", {
                "Compare t*(t>>5) e t*(t>>6). Compile e reinicie t em cada caso. Ouça o padrão mudar; mantenha o MASTER no mesmo nível.",
                "Compare t*(t>>5) and t*(t>>6). Compile and reset t for each. Listen to the changed pattern with MASTER at the same level.",
                "Comparez t*(t>>5) et t*(t>>6). Compilez et remettez t à zéro à chaque fois. Écoutez le motif changer au même niveau MASTER.",
                "Compare t*(t>>5) y t*(t>>6). Compile y reinicie t en cada caso. Escuche cambiar el patrón manteniendo el mismo MASTER."}},
            {"MIXER", {
                "Coloque células em dois stems. Use Solo para ouvir cada um, depois desative Solo e mova o pan de um deles. Ouça a posição mudar sem alterar a fórmula.",
                "Place cells on two stems. Solo each one, then disable Solo and move one pan control. Hear its position change without editing the formula.",
                "Placez des cellules sur deux stems. Écoutez chacun en Solo, désactivez Solo puis déplacez un panoramique. Écoutez la position changer sans modifier la formule.",
                "Coloque células en dos stems. Escuche cada uno en Solo, desactive Solo y mueva un panorama. Oiga cambiar la posición sin editar la fórmula."}},
            {"ESCALAS|SCALES|GAMMES", {
                "Com a mesma fórmula e Tom, compare maior, tons inteiros e Freygish. Use Fórmula → notas a cada troca. Observe o campo de alturas; o preset sozinho não cria um estilo musical.",
                "With the same formula and Tom, compare major, whole tone and Freygish. Use Fórmula → notas after each change. Hear the pitch field; a preset alone does not create a musical style.",
                "Avec la même formule et le même Tom, comparez majeur, tons entiers et Freygish. Utilisez Fórmula → notas après chaque changement. Écoutez les hauteurs ; un preset seul ne crée pas un style musical.",
                "Con la misma fórmula y Tom, compare mayor, tonos enteros y Freygish. Use Fórmula → notas tras cada cambio. Escuche las alturas; un preset solo no crea un estilo musical."}},
            {"TÔNICA|TONIC|TONIQUE|TÓNICA", {
                "Escolha Cadenciar e gere notas com Tom: Dó; repita com Tom: Ré. Ouça a última nota resolver na nova tônica. Volte a Dó para comparar.",
                "Choose Cadenciar and generate notes with Tom: Dó; repeat with Tom: Ré. Hear the last note resolve to the new tonic. Return to Dó to compare.",
                "Choisissez Cadenciar et générez des notes avec Tom: Dó, puis Tom: Ré. Écoutez la dernière note rejoindre la nouvelle tonique. Revenez à Dó pour comparer.",
                "Elija Cadenciar y genere notas con Tom: Dó; repita con Tom: Ré. Escuche la última nota resolver en la nueva tónica. Vuelva a Dó para comparar."}},
            {"FORMA DA FRASE|PHRASE FORM|FORME DE LA PHRASE|FORMA DE LA FRASE", {
                "Gere A–B–A–C e desative B em Variar frase. Ouça o espaço que ela deixa. Ativar todas as seções recupera a frase completa com a mesma seed e variação.",
                "Generate A–B–A–C and disable B in Variar frase. Hear the space it leaves. Ativar todas as seções restores the full phrase with the same seed and variation.",
                "Générez A–B–A–C et désactivez B dans Variar frase. Écoutez l’espace laissé. Ativar todas as seções restaure la phrase avec la même seed et variation.",
                "Genere A–B–A–C y desactive B en Variar frase. Escuche el espacio que deja. Ativar todas as seções recupera la frase con la misma seed y variación."}},
            {"CHAMADA E RESPOSTA|CALL AND RESPONSE|APPEL ET RÉPONSE|LLAMADA Y RESPUESTA", {
                "Insira uma frase de quatro compassos no Stem 1. Compare Resposta desligada e 1 → 2. Ouça o ritmo reaparecer com contorno invertido no Stem 2, uma barra depois da seção ativa.",
                "Place a four-bar phrase on Stem 1. Compare response off and 1 → 2. Hear its rhythm return with a mirrored contour on Stem 2, one bar after the active section.",
                "Placez une phrase de quatre mesures sur Stem 1. Comparez réponse désactivée et 1 → 2. Écoutez son rythme revenir avec un contour inversé sur Stem 2, une mesure après la section active.",
                "Inserte una frase de cuatro compases en Stem 1. Compare respuesta desactivada y 1 → 2. Escuche volver el ritmo con contorno invertido en Stem 2, un compás después de la sección activa."}},
            {"GLITCH", {
                "Selecione uma célula Bytebeat no banco. Aplique Glitch: XOR com intensidade baixa. Compare a nova célula com o pai; a derivação fica na grade e o original permanece no banco.",
                "Select a Bytebeat cell in the bank. Apply Glitch: XOR at low intensity. Compare the new cell with its parent; the derivative is placed in the grid while the original stays in the bank.",
                "Sélectionnez une cellule Bytebeat. Appliquez Glitch: XOR à faible intensité. Comparez la nouvelle cellule à sa source : la dérivée rejoint la grille et l’original reste dans la banque.",
                "Seleccione una célula Bytebeat. Aplique Glitch: XOR con intensidad baja. Compare la nueva célula con su origen; la derivación entra en la cuadrícula y el original queda en el banco."}},
            {"PAPEL MUSICAL|MUSICAL ROLE|RÔLE MUSICAL", {
                "Marque uma célula como baixo e escolha baixo no papel de uma pista. Use Sugerir posição e observe o destino proposto. O papel orienta a organização; não transforma o timbre da célula.",
                "Set a cell role to bass and set a track role to bass. Use Sugerir posição and observe the proposed destination. Roles guide organization; they do not change the cell timbre.",
                "Attribuez le rôle basse à une cellule et à une piste. Utilisez Sugerir posição et observez la destination proposée. Le rôle guide l’organisation sans changer le timbre.",
                "Asigne el papel de bajo a una célula y a una pista. Use Sugerir posição y observe el destino propuesto. El papel orienta la organización sin cambiar el timbre."}},
            {"ENTRADA ÚNICA|ONE INPUT AREA|UNE SEULE ZONE|UNA SOLA ZONA", {
                "Faça um traço ascendente e ouça com Ouvir texto/desenho. Alterne para Digitar texto, escreva uma frase curta e ouça. Volte a Desenhar: o traço anterior continua disponível.",
                "Draw a rising stroke and use Ouvir texto/desenho. Switch to Digitar texto, write a short phrase and listen. Return to Desenhar: the earlier stroke remains available.",
                "Tracez un trait ascendant et utilisez Ouvir texto/desenho. Passez à Digitar texto, écrivez une phrase courte et écoutez. Revenez à Desenhar : le trait reste disponible.",
                "Dibuje un trazo ascendente y use Ouvir texto/desenho. Cambie a Digitar texto, escriba una frase corta y escuche. Vuelva a Desenhar: el trazo sigue disponible."}},
            {"IDENTIDADE E VARIAÇÃO|IDENTITY AND VARIATION|IDENTITÉ ET VARIATION|IDENTIDAD Y VARIACIÓN", {
                "Gere uma frase, guarde-a e use Variar forma atual. Compare a variação com a célula guardada. Guardar célula preserva outra versão; Atualizar altera a célula selecionada em todas as ocorrências.",
                "Generate and store a phrase, then use Variar forma atual. Compare the variation with the stored cell. Guardar célula preserves another version; Atualizar changes the selected cell in all occurrences.",
                "Générez et conservez une phrase, puis utilisez Variar forma atual. Comparez la variation à la cellule conservée. Guardar célula garde une autre version ; Atualizar modifie toutes les occurrences de la cellule choisie.",
                "Genere y guarde una frase, y use Variar forma atual. Compare la variación con la célula guardada. Guardar célula conserva otra versión; Atualizar modifica todas las apariciones de la célula elegida."}},
            {"FÓRMULA COMO PARTITURA|FORMULA AS A SCORE|FORMULE COMME PARTITION|FÓRMULA COMO PARTITURA|FÓRMULA → NOTAS|FORMULA → NOTES|FORMULE → NOTES", {
                "Com a fórmula t^(t>>7), escolha Encadear e use Fórmula → notas. Depois escolha Cadenciar e gere de novo. Compare a última nota sem mudar escala, Tom, BPM ou MASTER.",
                "With t^(t>>7), choose Encadear and use Fórmula → notas. Then choose Cadenciar and generate again. Compare the last note without changing scale, Tom, BPM or MASTER.",
                "Avec t^(t>>7), choisissez Encadear puis Fórmula → notas. Choisissez ensuite Cadenciar et régénérez. Comparez la dernière note sans modifier gamme, Tom, BPM ou MASTER.",
                "Con t^(t>>7), elija Encadear y use Fórmula → notas. Elija Cadenciar y genere de nuevo. Compare la última nota sin cambiar escala, Tom, BPM ni MASTER."}},
            {"SOM AGORA", {
                "Use Ouvir célula e observe SOM AGORA. Depois use Tocar música e confira a mudança da fonte indicada. Use Parar antes de iniciar outra prévia se quiser comparar sem sobreposição.",
                "Use Ouvir célula and watch SOM AGORA. Then use Tocar música and check the indicated source change. Use Parar before another preview to compare without overlapping playback.",
                "Utilisez Ouvir célula et observez SOM AGORA. Passez à Tocar música et vérifiez la source indiquée. Utilisez Parar avant une autre préécoute pour comparer sans superposition.",
                "Use Ouvir célula y observe SOM AGORA. Pase a Tocar música y compruebe la fuente indicada. Use Parar antes de otra vista previa para comparar sin superposición."}},
            {"BANCO E GRADE|BANK AND GRID|BANQUE ET GRILLE|BANCO Y CUADRÍCULA|GRADE|GRID|GRILLE|CUADRÍCULA", {
                "Guarde uma célula e ouça-a no banco. Insira duas ocorrências na grade e deixe uma pausa entre elas. Tocar música revela a diferença entre material guardado e arranjo.",
                "Store a cell and preview it in the bank. Insert two occurrences with a gap between them. Tocar música reveals the difference between stored material and an arrangement.",
                "Conservez une cellule et écoutez-la dans la banque. Insérez deux occurrences séparées par un silence. Tocar música révèle la différence entre matériau conservé et arrangement.",
                "Guarde una célula y escúchela en el banco. Inserte dos apariciones con una pausa entre ellas. Tocar música revela la diferencia entre material guardado y arreglo."}}
        };
        const auto key = title.toUpperCase();
        for (const auto& item : exercises) {
            const auto headings = juce::StringArray::fromTokens(u(item.headings), "|", "");
            for (const auto& heading : headings)
                if (key.contains(heading)) return u(item.text[language]);
        }
        return fallback;
    }
    void tutorial() {
        struct Lesson { const char* title[4]; const char* action[4]; const char* listen[4]; };
        static const Lesson lessons[] = {
            {{"Criar uma entrada", "Create an input", "Créer une entrée", "Crear una entrada"},
             {"Clique em Digitar texto ou Desenhar. Escreva uma frase curta ou faça um traço. Ouvir texto/desenho toca a entrada ativa; alternar o modo preserva o outro rascunho.",
              "Click Digitar texto or Desenhar. Write a short phrase or draw a stroke. Ouvir texto/desenho plays the active input; switching mode keeps the other draft.",
              "Cliquez sur Digitar texto ou Desenhar. Écrivez une phrase courte ou tracez un trait. Ouvir texto/desenho joue l’entrée active ; changer de mode conserve l’autre brouillon.",
              "Pulse Digitar texto o Desenhar. Escriba una frase corta o dibuje un trazo. Ouvir texto/desenho reproduce la entrada activa; cambiar de modo conserva el otro borrador."},
             {"Compare um traço baixo e um alto. Ouça a mudança de registro; use Parar para interromper.",
              "Compare a low and a high stroke. Listen for the register change; use Parar to stop.",
              "Comparez un trait bas et un trait haut. Écoutez le changement de registre ; Parar arrête le son.",
              "Compare un trazo bajo y uno alto. Escuche el cambio de registro; Parar detiene el sonido."}},
            {{"Ouvir uma fórmula", "Hear a formula", "Écouter une formule", "Escuchar una fórmula"},
             {"Digite t*(t>>5) no campo da fórmula. Use Compilar fórmula e Fórmula dir. para a escuta Bytebeat. Reiniciar t retorna ao início do contador. Isso é uma prévia ao vivo, fora da grade.",
              "Type t*(t>>5) in the formula field. Use Compilar fórmula and Fórmula dir. for Bytebeat playback. Reiniciar t resets the counter. This is a live preview outside the grid.",
              "Saisissez t*(t>>5) dans le champ de formule. Utilisez Compilar fórmula puis Fórmula dir. pour le Bytebeat. Reiniciar t remet le compteur à zéro. Cette écoute est hors de la grille.",
              "Escriba t*(t>>5) en el campo de fórmula. Use Compilar fórmula y Fórmula dir. para Bytebeat. Reiniciar t reinicia el contador. Es una vista previa fuera de la cuadrícula."},
             {"Troque apenas 5 por 6, compile e reinicie t. Compare o padrão antes de fazer outra alteração.",
              "Change only 5 to 6, compile and reset t. Compare the pattern before making another change.",
              "Remplacez seulement 5 par 6, compilez et remettez t à zéro. Comparez le motif avant une autre modification.",
              "Cambie solo 5 por 6, compile y reinicie t. Compare el patrón antes de otra modificación."}},
            {{"Guardar uma célula", "Store a cell", "Conserver une cellule", "Guardar una célula"},
             {"Dê um nome ao material e use Guardar célula. O BANCO conserva materiais da música atual. Selecione a célula e use Ouvir célula para conferir o que foi guardado.",
              "Name the material and press Guardar célula. The BANK holds materials for this song. Select the cell and use Ouvir célula to check the stored result.",
              "Nommez le matériau puis utilisez Guardar célula. La BANQUE contient les matériaux du morceau. Sélectionnez la cellule et utilisez Ouvir célula pour vérifier le résultat.",
              "Nombre el material y use Guardar célula. El BANCO contiene los materiales de esta canción. Seleccione la célula y use Ouvir célula para comprobar el resultado."},
             {"Guarde duas versões com nomes diferentes. Alterne a seleção para comparar sem perder a primeira.",
              "Store two versions under different names. Switch selection to compare while keeping the first.",
              "Conservez deux versions sous des noms différents. Alternez la sélection pour comparer en gardant la première.",
              "Guarde dos versiones con nombres distintos. Alterne la selección para comparar conservando la primera."}},
            {{"Montar a música", "Arrange the song", "Construire le morceau", "Organizar la canción"},
             {"Escolha uma célula, duração, repetições e Stem. Use Inserir célula. Tocar música executa a GRADE. Arraste o centro do bloco para mover no tempo ou entre pistas; as bordas recortam o trecho.",
              "Choose a cell, duration, repeats and Stem. Use Inserir célula. Tocar música plays the GRID. Drag a block centre to move it in time or across tracks; its edges trim the excerpt.",
              "Choisissez cellule, durée, répétitions et Stem, puis Inserir célula. Tocar música joue la GRILLE. Déplacez le centre du bloc dans le temps ou entre pistes ; les bords découpent l’extrait.",
              "Elija célula, duración, repeticiones y Stem, y use Inserir célula. Tocar música reproduce la CUADRÍCULA. Arrastre el centro para mover el bloque; los bordes recortan el fragmento."},
             {"Repita uma célula e deixe um espaço antes da próxima. Ouça a pausa. Desfazer da grade reverte a colocação.",
              "Repeat a cell and leave a gap before the next. Listen to the rest. Grid Desfazer reverses the placement.",
              "Répétez une cellule et laissez un espace avant la suivante. Écoutez le silence. Desfazer annule le placement.",
              "Repita una célula y deje un espacio antes de la siguiente. Escuche la pausa. Desfazer revierte la colocación."}},
            {{"Variar e equilibrar", "Vary and balance", "Varier et équilibrer", "Variar y equilibrar"},
             {"Variar frase oferece formas e seções ativáveis. Em Fórmula → notas, escolha escala, Tom e movimento. Cada stem tem volume, pan, Mute e Solo. MASTER controla a saída geral.",
              "Variar frase offers forms and switchable sections. In Fórmula → notas, choose scale, Tom and motion. Each stem has volume, pan, Mute and Solo. MASTER controls the overall output.",
              "Variar frase propose formes et sections activables. Dans Fórmula → notas, choisissez gamme, Tom et mouvement. Chaque stem a volume, panoramique, Mute et Solo. MASTER règle la sortie générale.",
              "Variar frase ofrece formas y secciones activables. En Fórmula → notas elija escala, Tom y movimiento. Cada stem tiene volumen, panorama, Mute y Solo. MASTER controla la salida general."},
             {"Compare Encadear e Cadenciar com a mesma fórmula e Tom. Ouça a última nota; mantenha o nível constante.",
              "Compare Encadear and Cadenciar with the same formula and Tom. Listen to the last note at a constant level.",
              "Comparez Encadear et Cadenciar avec la même formule et le même Tom. Écoutez la dernière note à niveau constant.",
              "Compare Encadear y Cadenciar con la misma fórmula y Tom. Escuche la última nota a nivel constante."}},
            {{"Modelar altura por gesto", "Shape pitch with a gesture", "Modeler la hauteur par geste", "Modelar altura con un gesto"},
             {"Em Leitura / glifo escolha Gesto autoral · altura crescente, decrescente ou em arco. Edite o ponto central e escolha alcance de 6, 12 ou 24 semitons. O arco sobe e retorna à afinação original; força 0% recupera a fonte.",
              "In Leitura / glifo choose Gesto autoral · altura crescente, decrescente or em arco. Edit the middle point and choose a range of 6, 12 or 24 semitones. The arch rises and returns to the source pitch; 0% strength restores the source.",
              "Dans Leitura / glifo, choisissez Gesto autoral · altura crescente, decrescente ou em arco. Modifiez le point central et choisissez 6, 12 ou 24 demi-tons. L’arc monte puis revient à la hauteur source ; force 0 % retrouve la source.",
              "En Leitura / glifo elija Gesto autoral · altura crescente, decreciente o en arco. Edite el punto central y elija 6, 12 o 24 semitonos. El arco sube y vuelve a la altura original; fuerza 0 % recupera la fuente."},
             {"Compare 6 e 24 semitons na mesma entrada. Horários, duração, ordem e desenho ficam iguais; Guardar célula ou Atualizar conserva o gesto.",
              "Compare 6 and 24 semitones on the same input. Timing, duration, order and drawing stay intact; Guardar célula or Atualizar stores the gesture.",
              "Comparez 6 et 24 demi-tons sur la même entrée. Horaires, durée, ordre et dessin restent identiques ; Guardar célula ou Atualizar conserve le geste.",
              "Compare 6 y 24 semitonos en la misma entrada. Tiempos, duración, orden y dibujo permanecen iguales; Guardar célula o Atualizar conserva el gesto."}},
            {{"Salvar e retomar", "Save and return", "Sauvegarder et reprendre", "Guardar y retomar"},
             {"Salvar / WAV → Guardar versão captura composição e mix; Salvar projeto editável grava até 16 versões em árvore no .risco V75. Projetos V1–V74 continuam abrindo. O arquivo inteiro permanece limitado a 4 MiB. Exportar WAV oferece mix, stem ou lote e formato; antes do MASTER usa Float32.",
              "Salvar / WAV → Guardar versão captures arrangement and mix; Salvar projeto editável writes up to 16 tree-structured versions into .risco V75. Projects V1–V74 still open. The whole file remains limited to 4 MiB. WAV export offers mix, stem or batch and format; before MASTER uses Float32.",
              "Salvar / WAV → Guardar versão capture arrangement et mix ; Salvar projeto editável enregistre jusqu’à 16 versions arborescentes dans .risco V75. Les projets V1–V74 restent lisibles. Le fichier entier reste limité à 4 MiB. L’export WAV propose mix, stem ou lot ; avant MASTER, Float32.",
              "Salvar / WAV → Guardar versión captura arreglo y mezcla; Salvar projeto editável guarda hasta 16 versiones en árbol en .risco V75. Los proyectos V1–V74 siguen abriendo. El archivo completo conserva el límite de 4 MiB. WAV ofrece mezcla, stem o lote y formato; antes del MASTER usa Float32."},
             {"Guarde o projeto para persistir as versões. Retomar guarda o estado atual quando houver espaço; remova uma versão pelo submenu se a lista chegar a 16.",
              "Save the project to persist versions. Retomar stores the current state when space is available; remove a version from its submenu if the list reaches 16.",
              "Sauvegardez pour conserver les versions. Retomar garde l’état courant si une place existe ; supprimez une entrée dans son sous-menu si la liste atteint 16.",
              "Guarde el proyecto para conservar las versiones. Retomar guarda el estado actual si hay espacio; elimine una versión desde su submenú si la lista llega a 16."}}
        };
        pages.clear();
        for (const auto& lesson : lessons)
            pages.push_back({u(lesson.title[language]), u(lesson.action[language]), u(lesson.listen[language])});
    }
    int getNumRows() override { return int(pages.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool active) override {
        if (row < 0 || row >= getNumRows()) return;
        if (active) { g.setColour(juce::Colour(0xff25455c)); g.fillRect(0, 0, width, height); }
        g.setColour(active ? juce::Colour(0xff65d9e8) : juce::Colour(0xffafc3d1));
        g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 13.0f, juce::Font::plain)));
        g.drawFittedText(juce::String(row + 1) + "  " + pages[static_cast<size_t>(row)].title,
            12, 4, width - 24, height - 8, juce::Justification::centredLeft, 2, 1.0f);
    }
    void selectedRowsChanged(int row) override {
        if (row < 0 || row >= getNumRows()) return;
        selected = row;
        rememberedPage[static_cast<size_t>(currentTopic)] = row;
        card.page = pages[static_cast<size_t>(row)];
        const char* labels[] = {"EXPERIMENTE", "TRY THIS", "À ESSAYER", "PRUEBE"};
        card.label = u(labels[language]);
        card.accent = juce::Colour(row % 2 ? 0xffc4a1ff : 0xff65d9e8);
        card.layout(); view.setViewPosition(0, 0);
        previous.setEnabled(row > 0); next.setEnabled(row + 1 < getNumRows());
        position.setText(juce::String(row + 1) + " / " + juce::String(getNumRows()), juce::dontSendNotification);
    }
    std::vector<Page> pages;
    int language = 0, selected = -1;
    int currentTopic = 0;
    std::array<int, 3> rememberedPage{};
    Card card;
    juce::Viewport view;
    juce::ListBox index;
    juce::TextButton previous, next;
    juce::Label position;
};
