# Risco — créditos, proveniência e direitos das fórmulas

Atualizado em 2026-11-22. Direção artística, código e estudos iniciais da
máquina de funções: **Lúcio de Araújo**. O código próprio do Risco e os quatro
estudos iniciais gerados pela máquina V2 estão sob
**GNU AGPL-3.0-or-later**, conforme [LICENSE](LICENSE). O método aritmético
geral não é exclusivo; a licença identifica a expressão e a implementação
autoral incluídas no projeto. O instrumento está em incubação e o repositório
de código não foi publicado nesta etapa.

O repositório local `Risco/formulas.riscolib`, criado no diretório de dados do
usuário pelo aplicativo, é um **arquivo de performance**, não uma coleção
inteiramente coberta pela AGPL. Ele contém entradas com `name`, `expression`,
`author`, `source`, `sourceId` e `license`. As células da música guardam também
IDs dos pais das recombinações. O arquivo pode conter material do usuário e
terceiros com direitos diferentes; cada entrada deve ser avaliada separadamente
antes de redistribuição. O usuário conserva direitos sobre suas criações.

| Material | Crédito e origem | Tratamento no Risco |
| --- | --- | --- |
| Máquina V2, estudos iniciais e código próprio | Lúcio de Araújo / Risco, `risco:formula-machine-v2` | AGPL-3.0-or-later, incluídos no aplicativo/repositório local inicial. |
| Fórmulas importadas do [Dollchan Bytebeat Composer](https://github.com/SthephanShinkufag/bytebeat-composer) | Autor informado no item, URL e hash `dollchan:` | A licença MIT declarada para o player não é autorização geral das composições. Entrada marcada `rights-unverified`; importar é ato local do músico. |
| Fórmulas de [Viznut](https://viznut.fi/texts-en/bytebeat_algorithmic_symphonies.html), [Kragen](https://github.com/kragen/viznut-music), [ossia](https://ossia.io/score-docs/reference/languages/bytebeat.html) e outras coleções | Crédito de cada obra e proveniência precisam de revisão individual. | Referências de estudo; nenhuma expressão dessas coleções foi embutida nos presets iniciais. |
| Expressão colada pelo usuário | Origem não pode ser inferida pelo aplicativo. | Metadados vazios até atribuição explícita; revisar antes de compartilhar música ou banco. |
| Recombinação | Risco + autores indicados dos pais; IDs dos pais na música | Se algum pai tiver licença desconhecida, a filha recebe `rights-unverified`; recombinar não elimina direitos anteriores. |

O banco de uma performance pode ser salvo em arquivo V41 com autoria, origem,
identificador, licença e parentesco das funções. O repositório local usa o
formato V2; arquivos V1 abrem com licença vazia, que deve ser entendida como
não verificada. O campo `license` registra uma declaração de proveniência,
não substitui uma auditoria de direitos. Arquivar uma obra externa para uso
privado ou estudo não autoriza publicá-la como preset do Risco.

O Floatbeat e os FX de um polo/bitcrush são implementações próprias, sob
AGPL-3.0-or-later. O controle V7 em `src/core/LogisticControl.hpp` também é
implementação própria do autor, com aritmética inteira e contrato local;
nenhum código, figura, tabela ou texto de artigo foi incorporado. May (1976)
consta como referência documental de mapas iterados no Atlas, não como licença
de redistribuição do artigo nem como origem de código copiado.
`src/core/AyToneStudy.hpp` é um estudo parcial original
baseado nas relações de frequência descritas no
[manual General Instrument AY-3-8910](https://map.grauw.nl/resources/sound/generalinstrument_ay-3-8910.pdf)
([transcrição](https://www.silicon-heaven.net/atom/howel/parts/ay3891x_datasheet.htm)).
Nenhum código de emulador, tabela de DAC medida, ROM, fonte ou amostra nova
foi incorporado. O manual é referência técnica, não uma licença para
redistribuir seu texto/imagens. Esse estudo não é emulação fiel; a aproximação
linear de amplitude e a banda/envelope do Risco constam no README e no Atlas.

### Avaliação de núcleo AY/YM fiel — 2026-11-22

O núcleo [Ayumi](https://github.com/true-grue/ayumi), commit
`07c08b4874c359169e4a028edf73f046d8b763e2`, licença MIT, copyright Peter
Sovietov, foi incorporado em `third_party/ayumi`, acompanhado de `LICENSE` e
`README-RISCO.md`. Esses arquivos permanecem MIT; o código autoral do adaptador
`HistoricalAyEngine` permanece AGPL-3.0-or-later. Uma distribuição deve manter
o aviso e a licença do núcleo. O arquivo `ay8910.cpp` do
[MAME](https://github.com/mamedev/mame), commit
`6f9a2f023e1a951ba34f289d6a4d214a9578676b`, declara BSD-3-Clause e foi
consultado apenas como referência técnica; não foi copiado. Furnace e libvgm
permanecem referências externas, sujeitos às licenças próprias dos aplicativos
e de cada núcleo. A decisão e as lacunas técnicas estão em
[PESQUISA_NUCLEO_AY_FIEL.md](PESQUISA_NUCLEO_AY_FIEL.md).

`src/core/AyRegisterContract.hpp` e seu teste são código original do Risco,
AGPL-3.0-or-later. Eles decodificam máscaras e campos dos 16 registradores para
a bancada; não contêm áudio, tabelas DAC ou coeficientes externos e não são um
emulador.

`src/tools/ayumi_sandbox.cpp` é um adaptador original AGPL do Risco. O CMake
verifica os SHA-256 dos três arquivos do commit auditado e não baixa código. A
licença incorporada tem apenas um `LF` final acrescentado pelo fluxo de patch;
o texto é idêntico ao upstream, e ambos os hashes estão registrados em
`third_party/ayumi/README-RISCO.md`. Compilar e vincular código MIT com o Risco
não altera a autoria/licença daquele código.

O adaptador autoral `HistoricalAyEngine` e sua ligação ao transporte,
`SongRender`, exportação e formato V60 são AGPL-3.0-or-later. Eles não alteram
os arquivos MIT do Ayumi. O projeto preserva a separação entre a camada própria
de composição/estado e o núcleo externo, e documenta os limites técnicos em
`PESQUISA_NUCLEO_AY_FIEL.md`.

`src/tools/openmsx_ay_reference.tcl` é harness original do Risco,
AGPL-3.0-or-later. Na bancada de 22/11, openMSX 19.1, openMSX-data e C-BIOS
foram executados externamente a partir de pacotes Ubuntu extraídos em `/tmp`;
nada desses pacotes, ROMs, código GPL ou gravações foi incluído no Risco. Os
hashes e resultados estão em `PESQUISA_NUCLEO_AY_FIEL.md` e `TAREFAS.md`.

Fontes técnicas de síntese e seus créditos estão em
[FUNCOES_DE_SINTESE_E_ADAPTACOES.md](FUNCOES_DE_SINTESE_E_ADAPTACOES.md) e no
[Atlas RASGO](../../RASGO_DOCUMENTATION/ATLAS_DE_REFERENCIA_RASGO(4).md).
O detector de pico entre amostras em `src/core/OutputStage.hpp/.cpp` foi
adaptado do código de **Aquorbium**, do mesmo autor, sob AGPL-3.0-or-later
(commit `ab1da6b1b820cf06462fb26933e892a2db69f062`). Os hashes das fontes
consultadas e a validação constam em [TAREFAS.md](TAREFAS.md).

### Máquina de escalas do Trioio — adaptação local — 2026-11-16

`src/core/FormulaScaleMachine.hpp/.cpp` adapta ao gerador de eventos do Risco
as operações de rotação, alteração de grau, espelho e interpolação da máquina
de escalas do **Trioio**. Fonte: `PROJETOS_AUTORAIS/TRIOIO/src/core/ScaleMachine.h`
e `.cpp`, commit `9446b4ad74b9442744cafb5d0cdf03dce2448a45`; SHA-256,
respectivamente, `b00cc3d469b8d03414aae7e56afc76437942db88951ad18365c75b3c47ec595c`
e `2a8f36847a6693b584b1f6dbce69c2bc5200b3edfac9223c9f31749b593dbdd3`.
O Trioio declara GNU AGPL-3.0-or-later; a licença é compatível com a do Risco.
Os arquivos foram adaptados ao contrato fixo, limitado e determinístico de
`FormulaScaleMachine`; não se copiou `ScaleArchive`, `ScaleAffect`, catálogo,
dados de escalas nem dependência de build. A adaptação permanece AGPL-3.0-or-later.

Próxima revisão: oferecer edição de crédito/licença para fórmulas coladas e
exportação de um manifesto de proveniência antes de qualquer publicação.

## Interface, fonte e logo — 2026-11-15

O app local foi compilado com JUCE 9.0.3. Os módulos do framework são
oferecidos sob AGPLv3 ou licença comercial, conforme
[`JUCE-9.0.3/LICENSE.md`](../../third_party/JUCE-9.0.3/LICENSE.md).
O código do Risco declara AGPL-3.0-or-later; qualquer distribuição futura
precisa conferir o conjunto completo de obrigações do pacote.

A opção monospace da interface usa **DejaVu Sans Mono 2.37**, regular e bold,
copiados do pacote local `fonts-dejavu-core` 2.37-8 para `resources/fonts/`.
O CMake embute ambos no executável e o app registra as duas faces antes de
abrir a janela. Os avisos e a licença que devem acompanhar qualquer pacote
distribuído estão em [LICENSE-DejaVu.txt](resources/fonts/LICENSE-DejaVu.txt)
e na [página oficial DejaVu](https://dejavu-fonts.github.io/License.html).
Isso fixa a fonte usada no canvas, na opção terminal e no editor de fórmula;
testes em Windows e macOS continuam pendentes. As opções padrão e geométrica
ainda dependem das fontes do sistema e precisam de decisão tipográfica antes
de um release multiplataforma.

`resources/RiscoPixelLogo.svg`, `src/app/RiscoLogo.hpp` e os seis estudos em
`resources/RiscoLogosPropostas.svg` são desenho vetorial/pixel autoral do
projeto. Todas as letras da prancha são caminhos, sem arquivo de fonte externa;
A–C variam a composição e D–F propõem novas letras pixeladas.
`resources/RiscoLogosPixelNovas.svg` reúne três estudos adicionais de letras
pixeladas e composição de marca (G–I), igualmente autorais e sem fonte externa.
O favicon e os ícones em `resources/icons/` usam a onda quadrada assimétrica
autoral aprovada. O SVG é a fonte canônica; PNG e ICO são derivados gerados sem
material de terceiros. `resources/icons/onda-quadrada/` conserva a fonte da
versão aprovada e `resources/icons/onda-anterior/` preserva o conjunto anterior.

## Nome Risco — busca preliminar de disponibilidade em 2026-11-15

Uma busca pública na web por `Risco` associado a sintetizador, instrumento
virtual, plugin de áudio e software musical não identificou produto com o nome
exato no mesmo segmento. Essa observação não estabelece disponibilidade de
marca. Há usos de `Risco` em outros setores; antes de registro ou lançamento
comercial é preciso pesquisar os registros e sinais semelhantes nos países e
classes pretendidos, especialmente software e instrumentos musicais. Fontes
primárias para a próxima verificação: [INPI França](https://www.inpi.fr/ressources/propriete-intellectuelle/rechercher-une-marque-base-marques),
[EUIPO/TMview](https://www.euipo.europa.eu/en/trade-marks/before-applying/availability)
e [WIPO](https://www.wipo.int/en/web/madrid-system/check-availability).
O autor decidiu continuar usando **Risco** como nome do projeto. Nenhuma busca
completa nesses registros foi concluída; a decisão de nome não equivale a
liberação ou registro de marca.

Delay musical por stem (V42, 2026-11-21): implementação própria de buffer
circular com leitura interpolada, transição entre tempos e realimentação
limitada, por Lúcio de Araújo no projeto Risco, AGPL-3.0-or-later. Não incorpora
núcleo DSP, amostra ou código de terceiros. A documentação de pesquisa no
Atlas não é uma dependência deste efeito.

Reverb por stem (V44, 2026-11-21): rede própria de linhas com realimentação
amortecida e difusores, por Lúcio de Araújo no projeto Risco,
AGPL-3.0-or-later. Sem IR, amostra de sala, biblioteca DSP ou código de
terceiros incorporado; presets curto/longo não reproduzem equipamentos
históricos ou espaços acústicos específicos.

Automação de volume (V48, 2026-11-21): curvas de três pontos com interpolação
e suavização próprias, por Lúcio de Araújo no projeto Risco,
AGPL-3.0-or-later. Não incorpora editor, biblioteca ou código de automação
externo. Edição local por menu, integrada ao processamento e ao projeto Risco.

Biblioteca entre projetos (V50, 2026-11-21): mecanismo próprio de leitura,
busca e cópia de células, por Lúcio de Araújo, AGPL-3.0-or-later. Importar
conserva créditos/licença existentes e registra origem separadamente; não
atribui autoria ao importador nem muda a licença do material copiado. IDs
externos dos pais ficam no histórico, sem referências locais ambíguas.

Ruído do estudo AY (V51, 2026-11-21): implementação própria por Lúcio de
Araújo, AGPL-3.0-or-later. Especificação de período/mixer consultada na
[transcrição do manual General Instrument](https://www.silicon-heaven.net/atom/howel/parts/ay3891x_datasheet.htm),
sem copiar código, ROM, tabela medida, texto ou figura. A tabela interna é
calculada pelo algoritmo do projeto; polinômio/reset, mistura linear e
reamostragem são decisões do estudo, sem certificação de equivalência ao AY.

Envelope do estudo AY (V52, 2026-11-21): curvas de 16 níveis e temporização
implementadas por Lúcio de Araújo, AGPL-3.0-or-later. Referência técnica:
manual General Instrument, Envelope Period/Shape Control, divisor 256,
período de 16 bits e contador de 16 níveis. Sem código, gráfico ou tabela
externa incorporados. Retrigger por nota, curvas lineares e multiplicação
pela intensidade do Risco são decisões próprias, sem emulação fiel declarada.

LUFS (2026-11-21): cópia local adaptada do LoudnessMeter do Aquorbium,
commit `1064d55de38093595a35520fce0b0b59eb769463`, de Lúcio de Araújo,
AGPL-3.0-or-later. Teste de senos também adaptado desse commit; hashes de
origem em TAREFAS.md. Aquorbium registra origem no trabalho autoral do
Rasgo Modular. Namespace, limites de taxa, tratamento de denormais e
integração foram adaptados ao Risco; nenhum include/link depende do irmão.
Especificações consultadas: ITU-R BS.1770-5 e EBU Tech 3341 (2023), sem
redistribuir PDFs, figuras ou áudios de teste. Diagnóstico estéreo com
histograma de 0,1 LU, sem certificação EBU completa declarada.
