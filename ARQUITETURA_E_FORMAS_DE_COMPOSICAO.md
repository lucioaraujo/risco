# Risco — arquitetura e formas de composição

## Edição de partes na proposta

**Editar partes** permite escolher os canais de uma parte, ajustar sua
duração em blocos de quatro compassos, repetir o primeiro bloco e definir
dinâmica estável, crescendo, diminuendo ou **Arco**, que cresce até o centro e
recua no fim. Ao mudar a duração, as partes seguintes mudam de posição e usam
os mesmos motivos de referência. **Restaurar
automático** devolve a parte à decisão do gerador.

O Risco gera uma nova proposta com a mesma seed. O A/B conserva as duas
receitas para alternar entre a versão anterior e a editada. O arquivo salvo
guarda o resultado como células na grade e registra as escolhas na
proveniência. Em **Salvar → Reeditar partes da faixa salva**, o Risco lê essa
receita e reabre os controles depois que o projeto é aberto novamente.
**Aplicar** deriva outra proposta com a mesma seed e mantém a faixa carregada
como comparação A/B. Isso vale para projetos gerados em Por partes com receita
V4–V7 reconhecível; projetos feitos à mão e formatos antigos sem essa
proveniência continuam editáveis pela grade. Antes de exibir a ação, o Risco
regera a estrutura e compara células, eventos e posições; se a grade foi
alterada manualmente, a receita não é oferecida como editor ativo.

## Motivo e comparação na faixa completa

Com Texto ou Desenho como origem, uma faixa avulsa reaproveita os intervalos
e ataques da leitura sonora da inscrição. O motivo entra pela melodia quando
vem do texto e pela textura quando vem do desenho. A seed escolhe outro
canal para o retorno. A revisão mostra as duas pistas. O projeto guarda os
eventos e a receita do motivo, incluindo o canal de retorno.

**Nova seed** na revisão gera outra proposta com as mesmas opções e o mesmo
motivo. **Trocar A/B** alterna entre a proposta atual e a anterior em memória.
Cada uma pode ser ouvida e salva. Ao trocar de proposta, a prévia em curso
para e o botão **Ouvir prévia** fica disponível para a versão selecionada.

Na receita da faixa V2, a surpresa transforma apenas o retorno: no nível médio,
espelha os intervalos em torno da primeira nota; no alto, faz o espelho e
reverte a ordem dos ataques. O motivo original continua na abertura. O nível
baixo mantém o retorno literal. **Retorno do motivo** oferece Automático (padrão),
Literal, Espelho e Espelho + retrógrado para uma escolha manual independente da
surpresa. A proveniência V8 identifica a regra automática; V9 guarda uma escolha
manual. Projetos V4–V7 preservam a reprodução anterior.

## Motivo compartilhado no álbum

O planejamento aceita Número, Texto atual ou Desenho atual como origem.
Com uma inscrição, o Risco sugere a seed a partir da impressão digital e
reaproveita a leitura sonora existente para extrair até cinco ataques da voz
principal. O motivo guarda os intervalos em semitons e as distâncias rítmicas
quantizadas em meios tempos. O registro e a duração de cada nota se adaptam
ao canal e ao BPM da faixa.

O motivo aparece na abertura e no retorno. Em novos álbuns com inscrição, o
gerador V2 desenvolve o retorno por faixa: surpresa baixa mantém o motivo,
média espelha os intervalos e alta espelha e reverte a ordem dos ataques. O V1
dos manifestos antigos continua reproduzível. A linha muda entre melodia,
baixo e textura conforme a posição no álbum. A partir de quatro faixas,
a última recupera o motivo na melodia. No arranjo por partes, a pontuação
pode abrir uma pausa nas faixas de abertura, de colagem e de encerramento.
O desenho orienta a dinâmica. No perfil Contraste, algumas faixas internas
invertem a direção do crescendo.

O manifesto V6 guarda a versão do gerador, a impressão digital, o motivo
extraído e as escolhas
por faixa. **Álbum → Retomar projetos** lê o manifesto da pasta e usa os dados
derivados da inscrição. Projetos existentes compatíveis são reaproveitados.
A revisão do plano também nomeia a transformação que cada faixa aplicará ao
retorno, antes da criação dos projetos.
A retomada é interrompida ao encontrar um projeto incompatível. Os formatos V1 a V4
continuam disponíveis para álbuns de origem numérica.

## Arranjo por partes

O arranjo **Por partes** monta abertura, desenvolvimento, quebra, retorno e
final. A seed escolhe combinações de canais dentro de cada função musical.
A abertura pode soar com melodia, textura ou as duas. O desenvolvimento
acrescenta baixo e ritmo, a quebra reduz o conjunto e o retorno reúne os
cinco canais. Um final mais leve encerra a forma. Os blocos continuam usando
o BPM da faixa. Cada parte pode ter uma curva simples de intensidade.

O gerador cria na grade apenas as células dos canais que tocam. Cada
ocorrência continua editável e os espaços da grade representam as pausas dos
canais. A revisão da faixa indica o intervalo de compassos e os canais de
cada parte. O modo **Contínuo** preenche os cinco canais em cada bloco.

Na faixa completa, pontuação interna ou quebra de linha no texto pode
reservar o último compasso da quebra para a respiração. A direção vertical
média dos traços ativos define um crescendo ou diminuendo no desenvolvimento.
A seed e o material, junto das opções, reproduzem a proposta. O projeto
registra a seed, a impressão digital da inscrição e a indicação extraída.
Álbuns numéricos **Por partes** guardam esse modo no manifesto V4. Os
manifestos V1 a V3 usam o arranjo contínuo.

## Nova seed para faixa completa — 27 nov. 2026

O fluxo **Gerar faixa completa** oferece **Nova seed**, inspirado no botão
**SORTEAR UM PATCH** do aplicativo Rasgo Modular (referência de interação;
Risco mantém seu gerador e sua identidade). A ação sorteia uma variação nova e
gera uma proposta; não altera a música aberta. Com texto ou desenho como
origem, o fingerprint determinístico da inscrição participa da derivação da
nova seed e é guardado na proveniência. A seed numérica resultante reproduz a
faixa; para guardar a proposta, o músico pode ouvi-la e salvá-la no fluxo de
revisão.

Forma, surpresa, duração (32/64/80 compassos), pulso e paleta são escolhas
visíveis da receita e ficam registradas na proveniência da faixa. O arranjo
por partes usa as primeiras indicações de texto e desenho descritas acima.
Ainda resta mapear ritmo, pressão e contornos internos em motivos e decisões
de arranjo.

Estado operacional em 21 nov. 2026: formato V41, com frases V6 métricas,
compassos por seção e respostas harmônicas fixas, móveis e com intenção
explícita e V7 com controle logístico inteiro, seed/reset e custo limitado.
O autor testou o app e confirmou “ok”; o aceite não deve ser
convertido em nova rodada geral de validação. Outros processos com estado e demais
propostas abaixo continuam pesquisa onde não declaradas como implementadas.
README e abertura de TAREFAS governam a próxima execução.

Incremento implementado em 20 nov. 2026 (V33): subdivisão e 6/8 em eventos de
fórmula, harmonia/silêncio por compasso, edição de densidade/registro/dinâmica
das seções generativas, resposta por espaço/papel/registro, FX por stem,
interpretações de entrada, busca/favoritas, Floatbeat e estudo parcial de tons
AY. Contratos, operação e limites atuais no README e na fila de TAREFAS;
propostas históricas deste documento continuam pesquisa onde não assinaladas
como implementação. Não confundir AY parcial com emulação fiel.

Atualizado em 2026-11-21. Documento de pesquisa-criação. As propostas abaixo
orientam experimentos; não são declarações de novidade histórica, patente ou
funcionalidade já concluída.

## Hipótese artística

O Risco pode tratar uma inscrição como matéria que **gera funções, sons e
desenhos**, e cada resultado pode retornar à composição. O gesto do músico não
se limita a tocar notas ou disparar uma fórmula: ele pode escrever, desenhar,
alterar uma função, escolher o que lembrar, repetir uma célula ou permitir que
um stem influencie outro.

Uma função é, portanto, simultaneamente:

- uma regra sonora executável ou um procedimento que gera eventos;
- uma imagem de onda associada à realização daquela regra;
- um acontecimento com tempo, stem, fonte e linhagem na performance;
- material que pode ser chamado outra vez, transformado ou usado como
  influência, sem apagar a versão anterior.

## Invenções possíveis para o Risco

São linhas de investigação que podem formar uma identidade própria quando
forem realizadas, comparadas e avaliadas musicalmente:

### 1. Partitura de funções com memória visual

Cada função deixa uma imagem persistente da onda e um registro de como nasceu.
O músico pode percorrer o banco, escutar o desenho correspondente e recolocar
essa função em outro ponto da música. O histórico visual torna a memória da
performance manipulável, em vez de ser somente um histórico textual de código.

**Experimento:** gerar três funções a partir da mesma inscrição, guardar cada
onda, colocá-las em tempos diferentes e ouvir o reconhecimento e a diferença.

### 2. Composição por linhagens e descendência

Uma nova função pode derivar de uma anterior por mutação de parâmetros,
diferenças, integração limitada, alteração de operadores ou influência de um
sistema dinâmico. A nova versão guarda sua relação com a fonte; a fonte não é
substituída. O músico pode seguir uma linhagem, interrompê-la, voltar a um
ancestral ou combinar duas linhagens.

**Experimento:** manter o motivo e mudar só a densidade; depois manter o ritmo
e mudar a paleta harmônica; por fim comparar o original com o descendente.

### 3. Escrita como instrução e como matéria sonora

Um texto pode influenciar a geração por características observáveis — sequência
de caracteres, comprimento, espaços, pontuação, repetição, distribuição de
vogais e consoantes quando houver análise fonética apropriada. O compositor
escolhe quais características atuam em quais parâmetros. O significado pode
entrar por anotações ou interpretação opcional do músico, sem presumir que o
instrumento compreende o poema.

**Experimento:** usar a mesma frase em três leituras: forma gráfica, ritmo da
pontuação e interpretação anotada pelo compositor. Guardar cada leitura como
uma versão independente.

### 4. Contaminação consentida entre stems

Cada stem pode ser autônomo ou receber influência de outros. A influência é
uma rota explícita e regulável: por exemplo, o movimento de um stem altera a
densidade, o registro, a harmonia ou a próxima variação de outro. “Contaminar”
significa criar descendência rastreável, não modificar silenciosamente o
original. Um stem pode ser isolado enquanto os demais interagem.

**Experimento:** dois stems independentes; depois ligar somente uma rota de
influência (densidade do A para o registro do B) e comparar os três estados.

### 5. Forma musical por acumulação, retorno e ramificação

REC pode construir uma partitura de acontecimentos enquanto a música toca.
As funções podem acumular-se, repetir, reaparecer como refrão, responder a
outra camada, bifurcar em variações ou deixar lacunas. A forma emerge de
decisões do músico e regras de geração, sem exigir que toda composição comece
numa grade convencional de compassos.

**Experimento:** A → A' → pausa → B → retorno de A; comparar com uma forma de
camadas cumulativas em que cada nova função permanece audível.

Essas propostas aproximam campos já existentes — Bytebeat, música generativa,
composição algorítmica, sonificação e instrumentos visuais. A possível
contribuição do Risco deverá ser identificada pela combinação específica de
gesto, memória, retorno das imagens, relações entre stems e decisões musicais,
depois de pesquisa comparativa e protótipos escutados.

## Direção de desenvolvimento — máquina composicional por funções e gestos

Pedido do autor (16/11/2026): aprofundar teoria musical e cálculos matemáticos
para criar funções, recombinações e glitches matemáticos; desenvolver
variedade, nuances, dinâmicas, ápices, repetições, intenções, gestos, efeitos,
harmonias, ritmos, melodias, arranjos e organização dos stems.

A máquina deve operar em três escalas ligadas, mas distinguíveis:

1. **Função e cálculo.** Uma gramática tipada e limitada cria/transforma
   fórmulas. Operações podem atuar em amostras (Bytebeat), em uma função de
   áudio normalizada ou em descritores musicais a uma taxa de controle. A
   saída de cada operação declara domínio, unidade, faixa, estado, duração e
   custo; nenhuma expressão arbitrária é executada no callback.
2. **Composição.** A função pode gerar um objeto musical — motivo com ritmo,
   contorno, intervalos, acentos, pausas e articulação —, uma progressão ou
   uma regra de desenvolvimento. A semente reproduz o resultado, e fluxos
   independentes por função/stem evitam que editar o baixo refaça a melodia.
3. **Performance.** O músico conduz retenção, transformação, energia, entrada
   e saída de camadas com gestos graváveis. Mudanças estruturais podem esperar
   a batida, o compasso ou a frase; gestos de efeito podem agir imediatamente
   se forem suavizados e limitados.

### Vocabulário matemático a experimentar

- **Recombinação de funções:** soma ponderada, produto limitado, composição
  `f(g(t))`, troca por condição, máscara/modulação por outra função e operação
  modular/bitwise. Cada combinação preserva pais, parâmetros e resultado; o
  músico pode escolher o que herda (ritmo, contorno, timbre ou harmonia).
- **Glitch matemático intencional:** overflow e wrap modular, quantização,
  mudança de base/taxa, bit shifts/XOR, descontinuidade por limiar, trecho
  congelado, salto de fase, permutação de eventos e perturbação de mapa
  dinâmico. Cada gesto define intensidade, duração, probabilidade, região,
  retorno e seed. “Glitch” é uma transformação selecionável e reversível,
  nunca falha acidental ou permissão para valores ilimitados.
- **Variação com parentesco:** mutações alteram uma dimensão de cada vez e
  mantêm âncoras reconhecíveis. Repetição pode ser literal, ornamentada,
  transposta, invertida, retrógrada, aumentada/diminuída ou rearmonizada; o
  parentesco fica visível e a versão de origem continua recuperável.
- **Ritmo e desenvolvimento:** pulsos euclidianos e divisões métricas podem
  propor densidades; autômatos celulares, cadeias de Markov ou mapas iterados
  podem gerar mudanças com memória. Esses modelos são opções de processo, não
  estilos musicais prontos. Seed, limites e critérios de escuta devem ser
  explícitos.
- **Forma e intenção:** um grafo finito de seções pode organizar apresentação,
  desenvolvimento, contraste, ápice, rarefação e retorno. Intenções são
  parâmetros nomeados pelo autor/músico (por exemplo, suspender, acumular,
  interromper, responder), não rótulos emocionais deduzidos automaticamente.
  O ápice pode resultar de densidade, registro, dinâmica, tensão harmônica e
  atividade de efeitos, e não apenas de volume.

### Contratos musicais para os primeiros protótipos

- **Harmonia:** começar com paleta e tonalidade/modo explícitos; escolher
  progressões por um grafo ponderado com opções de desvio, repouso e retorno.
  Resolver vozes minimizando deslocamentos, preservando notas comuns e
  respeitando tessitura. Permitir cor modal, cromatismo ou atonalidade como
  escolhas distintas, sem impor tonalidade funcional a todo material.
- **Melodia:** compor sobre ritmo e esqueleto harmônico; controlar contorno,
  centro/registro, passos e saltos, notas-alvo em tempos fortes, respiração,
  motivo e resolução. Restrições são preferências graduáveis; exceções podem
  ser gestos autorais.
- **Arranjo e stems:** separar papel musical de stem físico. Cada camada declara
  função (pulso, baixo, harmonia, melodia, textura/gesto), faixa, densidade,
  envelope de entrada/saída e dependências. As três pistas atuais podem
  continuar livres; um mapa de papéis opcional orienta a geração sem fixar a
  identidade das pistas. Mute/solo e automação não alteram a fonte geradora.
- **Dinâmica e efeitos:** curvas de energia por seção e stem controlam acento,
  duração, densidade, articulação, registro e níveis. Efeitos criativos
  (bitcrush, filtro, delay, distorção, reverberação e cortes) recebem gestos e
  envelopes próprios. A proteção técnica de saída permanece depois deles.
- **Gesto:** desenhar, arrastar, marcar trecho, repetir, silenciar, gravar ou
  mudar um controle pode atuar como comando musical. Cada gesto declara alvo,
  alcance, resolução, suavização e se fica registrado; sua ação deve ser
  audível e comparável com o mesmo seed sem o gesto.

### Métrica, repetição e critérios para improvisar — decisão de 2026-11-16

O Risco combina dois modelos que já têm precedente técnico: o
[Bytebeat Composer](https://github.com/SthephanShinkufag/bytebeat-composer/blob/master/src/audio-processor.mjs)
usa um contador de amostras para as fórmulas; o
[Furnace](https://github.com/tildearrow/furnace/blob/master/doc/1-intro/concepts.md)
reutiliza padrões numa lista de ordens, e sua
[FAQ](https://github.com/tildearrow/furnace/blob/master/doc/1-intro/faq.md)
orienta ajustar comprimento e destaques de linha para compassos como 5/4 ou
6/8. O [FamiStudio](https://famistudio.org/doc/song/index.html) permite
comprimentos diferentes por padrão e explica que o compasso resulta do
agrupamento das notas, sem uma assinatura métrica intrínseca. Esses
precedentes não obrigam o Risco a copiar a interface de um tracker.

**Estado implementado:** o contador Bytebeat direto é contínuo; o compasso
global pode ser escolhido de 2/4 a 7/4 e é salvo; uma célula pode repetir
1–8 vezes na grade e na pré-escuta. O destino é manual pelo seletor Stem ou
pelo clique na fileira; Auto: frases/REC acrescentam células após a última no
stem escolhido. A opção **Sugerir posição** no seletor calcula uma alternativa
para uma célula já escolhida no banco, sem alterar a grade: alinha ao próximo
compasso, examina no máximo 16 compassos, rejeita colisão no próprio stem,
penaliza simultaneidade e duplicação do mesmo ID e favorece a continuidade
do mesmo motivo no stem de origem quando próximo. Mostra tempo, stem e
concorrência; o músico só confirma pelo botão Inserir célula. Para células com
eventos musicais disponíveis, calcula densidade por compasso, registro MIDI
médio, fração ativa e RMS estimado da onda guardada; Bytebeat sem eventos de
notas usa apenas atividade estimada pela onda e RMS. **Equilibrar som** penaliza
simultaneidade entre stems quando ambos os materiais são ativos e têm nível
mensurável; densidade de eventos refina o peso quando disponível, e pausas
abrem espaço. O resumo aparece no estado e os valores completos no tooltip.
Isso mede dados do material
registrado, não reconhece semanticamente o timbre nem prova que o rótulo
corresponde ao áudio. Cada célula pode declarar **livre, pulso, baixo, harmonia,
melodia ou textura**. Cada pista também declara um papel nessa lista, editável
no seletor à esquerda da fileira e salvo em V24; arquivos V1–V23 recebem o
padrão melodia/harmonia/baixo, com Ritmo e Textura adicionais na extensão
opcional `STEM_EXTRAS`. A sugestão favorece o encaixe entre o papel da
célula e o papel da pista, além de considerar continuidade e simultaneidade.
Esses papéis orientam a distribuição, mas não alteram a síntese nem movem
ocorrências já colocadas. O papel da célula é uma intenção editável no banco e
persistida no formato V14; arquivos V1–V13 entram como livre. A sugestão favorece continuar o mesmo
papel no stem, penaliza papéis distintos no mesmo stem e simultaneidade do
mesmo papel em stems diferentes. Papéis diferentes podem coexistir.
A regra não faz classificação automática de timbre, função harmônica ou
headroom, nem verifica se o áudio corresponde ao papel declarado. A resposta
entre stems é opt-in e limitada a uma repetição transposta por bloco; não cria
feedback recursivo, mas ainda precisa de avaliação auditiva. A frase V2 conserva um desenho interno de 4/4 e é
redimensionada pelo arranjador para a duração do compasso selecionado; isso
ajusta a extensão, mas ainda não cria acentos próprios de 3/4, 5/4 ou 7/4.
Na leitura **Fórmula → notas**, os ataques de semicolcheia recebem acentos
agrupados: em 5/4, 3+2 ou 2+3; em 6/4, 3+3 ou 2+2+2; em 7/4, 2+2+3,
3+2+2 ou 2+3+2. O menu permite escolha manual ou automática; esta deriva o
grupo do primeiro byte da expressão, de forma determinística. A prévia anuncia
a opção. V23 salva a preferência manual e V1–V22 usam o modo automático; não há
6/8 nem mudança métrica por seção. Portanto, a geração atual é uma
improvisação determinística limitada, sem escuta contextual.

**Próxima camada composicional:** a inserção automática deve ser uma sugestão
visível e reversível, não uma mudança silenciosa da grade. Primeiro, verificar
fonte válida, limite de células, duração e headroom. Depois pontuar candidatos
por espaço livre, papel musical (pulso, baixo, harmonia, melodia, textura),
densidade simultânea, registro, contraste de timbre, continuidade do motivo,
resposta à frase anterior e curva de energia da seção. A saída deve mostrar
"por que aqui e neste stem", permitir ouvir antes/depois e guardar seed,
versão da regra e decisão do músico. Sobreposição é uma opção musical
explícita, com medição de nível; não é erro a ser sempre eliminado.

Uma primeira rota de interação pode usar **o pulso do stem A para modular a
probabilidade de resposta do stem B no compasso seguinte**, com intensidade
limitada e opção desligado. A informação circula como eventos ou descritores
por compasso, preservando as fórmulas originais e evitando feedback de áudio
sem limite. Outras rotas possíveis: contorno melódico que orienta uma resposta,
envelope que abre espaço, ou motivo harmônico que restringe notas-alvo. O
motor deve escolher quando *não* acrescentar timbre, harmonia ou melodia: a
pausa e a rarefação fazem parte da variação. Validar musicalmente com a mesma
seed e três estados: rota desligada, rota fraca e rota forte.

Antes de chamar isso de improvisação contextual, construir um plano de seção
com apresentação, repetição reconhecível, desenvolvimento, contraste,
possível ápice e retorno. Cada célula precisa declarar papel, duração,
possíveis transformações e o que deve permanecer estável. O usuário poderá
aceitar, editar ou rejeitar sugestões sem perder a origem nem a posição de
retomada.

### Texto e desenho como fontes composicionais

- **Desenho:** preservar pontos, ordem temporal dos traços, pausas e cruzamentos.
  Extrair descritores nomeados — direção, extensão, velocidade quando houver
  timestamps, curvatura, densidade de pontos, altura, repetição e simetria —
  sem substituir o gesto original pelo contorno final. O autor escolhe o
  descritor e o destino (contorno melódico, ritmo, registro, articulação,
  dinâmica, harmonia ou forma). Reconhecer um glifo é uma leitura opcional e
  separada do gesto de desenhá-lo.
- **Texto:** manter os caracteres UTF-8 intactos e oferecer leituras formais
  configuráveis: duração de palavras/caracteres, espaços e pontuação como
  silêncio, repetição, comprimento de frase, acentos e distribuição de
  caracteres. Leitura fonética deve ser opt-in, identificar idioma/método e
  permitir correção; o mapeamento direto de código Unicode para altura não
  deve ser apresentado como leitura musical natural nem semântica.
- **Composição a partir da fonte:** além de converter cada elemento em nota,
  usar texto/desenho para criar um motivo que depois passa por harmonia,
  variação e forma. Mostrar a cadeia de transformação (fonte → descritores →
  parâmetros musicais → função/eventos → stems) e permitir ouvir cada etapa.
- **Gesto de autoria:** o desenho em tempo real pode abrir/fechar uma frase,
  indicar crescendo, densidade ou intenção; o texto pode ser partitura,
  instrução ou matéria fonética, mas o modo deve ser escolhido. Guardar a
  escolha e a versão dos mapeamentos com a célula para que a seed não esconda
  mudanças de regra.

### Filtros e efeitos como parte da composição

- Separar **efeito criativo** (timbre/forma) da **proteção de saída**. Filtros,
  saturação, bitcrush, delay, eco e reverberação podem ser transformados por
  fórmulas e gestos; limiter, saneamento de não finitos e ceiling ficam no
  último estágio técnico e nunca são modulados pela geração.
- Organizar efeitos em inserts e sends por stem, com retorno e ganho explícitos.
  A atribuição de papel aos stems pode oferecer pontos de partida (por exemplo
  filtro no baixo, espaço na textura, delay na resposta), mas não bloquear
  combinações livres. Mostrar o caminho do sinal e permitir solo do retorno.
- Automatizar parâmetros por compasso, envelope, curva, gesto ou função
  limitada; permitir sincronização em batida/divisão ou tempo em ms/Hz. Mudanças
  rápidas recebem smoothing ou crossfade. Feedback de delay/reverb tem limite
  estável; filtros não podem receber frequências fora da faixa válida.
- Distinguir um **glitch de efeito** (gate, buffer hold, stutter, freeze,
  reversão de trecho, drop ou corte) de **glitch matemático** (wrap, troca de
  operador, salto de fase, quantização). Ambos precisam de janela, intensidade,
  probabilidade, duração, seed e gesto de retorno; uma transformação só afeta
  o trecho/material escolhido.
- Filtros iniciais a explorar: passa-baixa/alta/ faixa e notch com frequência,
  ressonância e envelope; depois filtro em escada ou comb se houver razão
  musical. Efeitos iniciais: bitcrush/quantização, delay sincronizado,
  reverb de memória curta e distorção com controle de drive. Medir custo e
  aliasing antes de oversampling; nonlinearidades que possam gerar picos ficam
  antes da saída técnica.
- A neutralidade deve ser clara: mix 0/bypass soa como a rota sem o efeito;
  wet/dry, ganho compensado e nível do retorno não podem fazer o efeito parecer
  melhor apenas por ficar mais alto. A automação e o preset devem persistir
  com proveniência e ser desfeitos sem perder a fonte.

### Organização e acesso dentro do instrumento

- Tratar **projeto**, **função-fonte**, **variação/descendente**, **célula**,
  **ocorrência na grade**, **stem** e **take de áudio** como objetos ligados,
  mas diferentes. A ocorrência aponta para uma célula; a célula aponta para a
  fonte ou receita e pode registrar uma transformação; editar uma ocorrência
  não altera silenciosamente as demais.
- Cada objeto recebe ID estável independente do nome. O nome é livre e pode
  mudar; origem, autor, seed, versão da regra, pais, data, papel musical,
  duração, resolução e parâmetros são metadados explícitos. Arquivos devem ser
  recuperáveis sem depender de posição/ordem da grade ou do nome da pasta.
- Retomar deve ser um gesto simples: **Recentes/Continuar** reabre o projeto e
  restaura seleção, página de trabalho, célula/trecho e zoom relevantes; o
  transporte volta parado por padrão, sem iniciar som inesperadamente. Seeds,
  versão do gerador, parâmetros e ordem dos eventos ficam salvos para a mesma
  composição voltar a soar igual. Mudança de versão do motor não reescreve
  silenciosamente uma música antiga.
- Manter recuperação automática em snapshot separado do último salvamento
  confirmado, com hora e projeto identificados. Ao encontrar recuperação,
  oferecer abrir, comparar ou descartar; nunca substituir a última versão
  válida sem escolha. Checkpoints nomeados devem ser fáceis de criar antes de
  recombinações ou mudanças grandes.
- Tornar cada material fácil de editar em seu nível: fórmula e parâmetros da
  função, notas/eventos do motivo, progressão, automação, papel/nível do stem e
  ocorrência na grade. Edição pode ser direta ou regenerar uma dimensão
  escolhida; preservar as demais dimensões quando possível e mostrar a
  diferença. “Converter em áudio” cria derivado, não substitui a estrutura
  musical editável.
- Desfazer/refazer cobre edição, gesto, transformação, movimento e operação de
  grade. A fonte original e os checkpoints continuam acessíveis; edição de uma
  ocorrência pode ser local ou propagada às demais, mas o alcance é escolhido
  e explicado antes da alteração.
- Manter o arquivo de projeto portátil como unidade principal. Pastas são uma
  organização opcional para projetos e exportações, não cópias rígidas dos
  painéis da interface. Um formato textual versionado deve permitir inspeção,
  cópia de segurança e migração; referências órfãs devem ser detectadas e
  recuperáveis.
- O acesso cotidiano deve ser pelo próprio Risco: um **Navegador** integrado
  com Projetos recentes/abertos, banco de Células e Funções, busca, filtros
  (stem/papel, origem, duração, seed, descendência), favoritos, nomes e
  metadados. A pré-escuta identifica o que toca; ações de inserir, substituir,
  duplicar, derivar, comparar e abrir na grade ficam no contexto do item.
- O navegador mostra linhagem e vizinhança sem exigir árvore de pastas: fonte
  → transformação → descendentes → células → ocorrências/takes. Uma visão de
  pastas pode existir como painel secundário para importar/exportar e refletir
  os arquivos no disco; banco e navegação permanecem úteis quando o autor não
  lembra o caminho físico.
- Importar/exportar é explícito e local, com confirmação de formato e direitos
  quando houver material externo. Nenhuma sincronização de fórmulas, envio à
  rede ou inclusão automática de conteúdo de terceiros. Pré-escuta, busca e
  indexação não bloqueiam nem alocam no callback.
- Começar com busca, lista, metadados, pré-escuta e inserção/derivação; adiar
  tags complexas, múltiplas bibliotecas, banco externo ou organização em árvore
  até o uso real mostrar necessidade. Teclado, estado vazio, nomes longos e
  desfazer fazem parte da validação.

### Learn, tutoriais e idiomas

- Fazer de **Learn** um guia consultável por conceito e controle: função,
  transformação, gesto, stem, harmonia, ritmo, forma, dinâmica, glitch,
  filtro/efeito e saída sonora. Cada ficha explica em linguagem direta o que
  entra, o que a regra calcula, o que pode mudar, o que fica preservado e onde
  o resultado aparece.
- Usar exemplos vivos dentro do Risco: ouvir fonte e descendente com o mesmo
  seed/andamento; destacar a célula, os eventos, os parâmetros e os stems
  afetados; permitir restaurar o exemplo ou derivar dele. Uma explicação deve
  corresponder ao algoritmo da versão corrente, com versão da regra indicada.
- Organizar tutoriais como percursos curtos e progressivos (primeiro som,
  primeira célula, frase e repetição, harmonia/arranjo, máquina de funções,
  gestos e performance, efeitos, salvar/exportar). O autor pode interromper,
  voltar ou abrir ajuda contextual sem perder o trabalho.
- Manter **PT, EN, FR e ES** alinhados em controles, Learn, tutoriais, mensagens
  de erro e exemplos; ao faltar tradução, identificar o idioma de fallback em
  vez de misturar silenciosamente. Fórmulas, nomes de parâmetros, valores,
  seeds e notação musical permanecem idênticos entre idiomas, para que o mesmo
  exercício seja reproduzível.
- A parte didática não atribui intenção emocional automática nem promete que
  um cálculo é musicalmente bom. Explica possibilidades e limites e convida a
  ouvir/decidir; a escuta e a escolha do compositor continuam centrais.

### Facilidade de tocar, orientação e agência

- O primeiro percurso deve permitir **ouvir, intervir e guardar** com poucos
  gestos, sem exigir fórmula, teoria musical formal ou configuração de áudio.
  A geração avançada expande a prática; não é requisito para fazer música.
- Manter uma ação principal clara por etapa e agrupar controles pelo percurso
  musical (criar/escutar → desenvolver → organizar/tocar → gravar). Mostrar
  primeiro os controles necessários ao gesto atual; avançados aparecem sob
  demanda e podem ser reabertos sem perder o contexto.
- Evitar excesso de parâmetros simultâneos, nomes sem explicação e estados
  escondidos. Cada controle indica em linguagem direta o que afeta, qual
  unidade/faixa usa, quando a mudança entra e se altera a fonte, uma variação,
  uma ocorrência ou a mixagem. Usar defaults que deem resultado audível e
  musicalmente legível.
- Antes de operações estruturais, deixar claro o alvo e a consequência;
  sempre que viável oferecer pré-escuta/preview. Depois, mostrar o que mudou,
  o que foi preservado e como desfazer/recuperar. O músico deve conseguir
  testar sem receio de destruir a versão original.
- Manter feedback contínuo: estado da fonte que soa, seed/regra, célula e stem
  ativos, próxima transição, efeito ouvido e automação em curso. Não depender
  de memória de atalhos ou de abrir Learn para descobrir se REC está armando,
  gravando ou parado.
- Validar com tarefas reais e observação: pessoa consegue criar uma frase,
  repetir/transformar, trocar uma camada, recuperar a origem e salvar sem se
  perder? Contar controles/decisões por tarefa, erros e necessidade de ajuda.
  Reduzir a interface quando o mesmo resultado puder ser expresso com menos
  escolhas visíveis; não remover profundidade do motor.
- Validar continuidade: salvar, fechar/reabrir, retomar e recuperar um snapshot
  interrompido; confirmar seleção, seed, parâmetros, eventos e desfazer/refazer.
  Simular atualização do formato e confirmar que uma composição antiga mantém
  sua versão de geração ou oferece migração revisável.

### Sequência de pesquisa e aceitação

1. Gerar, com a implementação atual, uma frase de 4 compassos e identificar
   motivo, progressão, papéis, dinâmica, repetição e pontos de tensão/repouso.
2. O menu **Variar frase** oferece os blueprints V2 A–A'–contraste–retorno,
   V3 A–B–A–C e V4 A–B em dois compassos, guardando a versão na célula.
   As seções e os papéis por stem já são editáveis; o estado mostra as seções
   ativas ao gerar ou selecionar uma célula. Próximo: comparar três seeds e
   escutar os blueprints com BPM e nível iguais.
3. Fazer uma única família de recombinação matemática sobre uma função
   autoral; expor pais, parâmetros e faixa de saída. Comparar o resultado como
   áudio e como eventos musicais.
4. Prototipar um glitch matemático controlado (por exemplo wrap/quantização ou
   salto de fase) com gesto, duração e recuperação; verificar que a forma
   musical continua legível e que o pico segue protegido.
5. Melhorar o mapeamento de texto/desenho em uma etapa auditável e editável;
   comparar descritores e resultado com a mesma fonte.
6. Prototipar o Navegador interno com dados locais; confirmar que o autor
   encontra, escuta, deriva e insere um material sem sair do instrumento.
7. Ligar Learn/Tutorial a exemplos reproduzíveis e conferir paridade PT/EN/FR/ES.
8. Fazer um percurso guiado curto e testar se o usuário entende fonte,
   transformação, estado de reprodução e desfazer sem se perder nos controles.
9. Só então ampliar harmonia, métricas, polirritmia, papéis de stems e efeitos.

Critérios: repetibilidade por seed e versão; motivo reconhecível sem
uniformidade; variação perceptível e rastreável; transições ritmicamente
coerentes; tessitura e densidade controladas; contraste de dinâmica sem
dependência exclusiva do volume; saída limitada e ausência de custo não
limitado. A aprovação musical depende de escuta humana, não apenas de métricas.

## Arquitetura conceitual

```text
Texto / desenho / fórmula / gesto
                 │
                 ▼
     Leitura e descritores escolhidos
                 │
                 ▼
     Motor de funções e sistemas dinâmicos
                 │
          ┌──────┴──────┐
          ▼             ▼
  Eventos musicais   Função de áudio
          │             │
          └──────┬──────┘
                 ▼
        Stem(s) e rotas de influência
                 │
                 ▼
   Performance: banco + grade + linhagem
       │         │             │
       ▼         ▼             ▼
      REC      retorno      desenho de onda
                 │
                 ▼
     Síntese/mistura/saída de áudio
```

### Camadas e responsabilidades

1. **Fontes:** texto, desenho, função editada, arquivo de fonte, gesto e dados
   externos. Preservar a fonte original e a sua codificação.
2. **Leitura:** extrair descritores geométricos, tipográficos, textuais ou
   temporais. Cada descritor deve indicar método e parâmetros; análise
   semântica é opcional e não substitui o texto-fonte.
3. **Geração:** criar expressão Bytebeat, função Floatbeat, sistema dinâmico,
   sequência de eventos ou combinação. Usar gramática limitada, compilação
   segura e orçamento previsível; não executar código arbitrário no áudio.
4. **Stem:** manter identidade, estado, nível, resolução/sonoridade e coleção
   de funções. O relógio musical pode ser comum, com fase, divisão ou duração
   de ciclo próprios por stem.
5. **Influência:** grafo dirigido de rotas entre stems. Cada rota declara
   origem, destino, parâmetro, intensidade, limites e quando atua. Influências
   afetam estados futuros ou criam descendentes registrados; nunca apagam a
   fonte original.
6. **Performance:** log temporal de funções e gestos; banco reutilizável;
   grade ou outras formas de organização. Um evento referencia sua versão
   imutável, instante musical, stem e linhagem.
7. **Renderização:** combinar vozes e funções sem alocar ou bloquear no
   callback. Separar transporte e relógio de amostras, síntese, quantização,
   taxa de amostragem e formato do dispositivo.
8. **Interface:** uma tela principal reúne escrita/desenho, execução, REC,
   master, BPM, banco, grade, stems, influência e visualização. Painéis
   auxiliares podem recolher conteúdo, mas a performance não fica repartida em
   abas desconexas. O protótipo JUCE usa canvas lógico 1280×900; a captura em
   1920×1080 confirma ocupação da largura. O redimensionamento aplica fatores
   X/Y independentes e a janela ainda limita o mínimo a 1280×900, portanto
   proporções distintas e workspaces baixos pedem um modo compacto validado.

## Eixos de síntese: não confundir categorias

| Eixo | Opções iniciais | O que descreve |
| --- | --- | --- |
| Cálculo | Bytebeat, Floatbeat, funções matemáticas, sistemas dinâmicos | Como a fonte produz valores ou eventos |
| Quantização | 8-bit, 16-bit | Resolução de amplitude escolhida para a saída |
| Taxa | por exemplo, relógio Bytebeat a 8 kHz e dispositivo a 44,1/48 kHz | Quantas amostras por segundo |
| Síntese/timbre | pulso, triângulo, ruído, osciladores, amostras, FM | Como os valores são convertidos em matéria sonora |
| Estética | chiptune, lo-fi, limpa, híbrida | Referência e caráter percebidos; pode combinar outras escolhas |
| Arranjo | mono, stems independentes, stems influenciados, híbrido | Como fontes e camadas se relacionam |

“Híbrida” descreve combinação de fontes/sínteses; pode usar quantização de 8
ou 16 bits. Bytebeat clássico frequentemente usa inteiros de 8 bits, mas a
resolução da saída e a estética de timbre devem continuar escolhas distintas.
Chiptune é uma referência estética ligada a sons e limites de sistemas de
jogos/computadores; não fixa por si só o número de bits nem a arquitetura do
sintetizador.

### Combinar Bytebeat, Floatbeat, sínteses e eletrônica

Pedido do autor: pesquisar síntese matemática contemporânea junto a Mega Drive,
Commodore Amiga/Paula, Super Nintendo (SPC700 + S-DSP), Mega Drive,
Yamaha YM3812/OPL2 e DX7, Roland TB-303/TR-808, Fairlight CMI, SID/NES, Atari 2600 TIA, POKEY,
PSGs SN76489 (Texas Instruments) e AY-3-8910, FM, granular, wavetable, CMOS
e circuitos de componentes eletrônicos. Tratar
cada uma como arquitetura ou processo que pode ocupar uma etapa do fluxo,
em vez de selecionar uma família e excluir as outras.

| Nível de combinação | Fonte → destino | Exemplo musical |
| --- | --- | --- |
| Amostras | Bytebeat/Floatbeat → waveshaper → quantizador | expressão rítmica vira áudio, recebe uma curva e é comparada em 8/16 bits |
| Oscilador | caos/fractal → fase, frequência ou wavetable | mapa cria uma trajetória; leitura periódica mantém altura e afinação controláveis |
| Timbre | Mega Drive, Paula, SNES S-DSP, OPL2/DX7, CZ, TB-303, TR-808, Fairlight CMI, TIA, POKEY, PSGs, wavetable, granular, NES ou SID → mixer | motores diferentes ocupam stems, com envelopes e limites de nível próprios |
| Controle | equação ou sistema dinâmico → parâmetros de síntese | equação modula índice FM, largura de pulso, filtro, ganho, posição/tamanho de grão |
| Eventos | funções matemáticas → alturas, durações, acentos e pausas | saída simbólica guiada por escala, métrica, papel musical e regras de frase |
| Circuito | modelo de componentes → osciladores, filtro, clipping e realimentação | circuito discreto virtual produz a matéria sonora; gestos alteram componentes nomeados |
| Forma | processo fractal ou caótico → decisões por seção | densidade, repetição, rarefação e ápice evoluem em escala de frase |

Uma mesma função caótica pode gerar amostras, modular um parâmetro lento ou
propor eventos. Esses modos têm resultados e riscos diferentes. Um caminho
combinado possível é **mapa caótico → posição de wavetable → FM leve →
envelope musical → stem**, com seed, faixa e taxa de controle visíveis.
Outro é **texto/desenho → eventos → vozes NES/SID virtuais → filtro**. Já a
modelagem eletrônica pode representar circuitos CMOS ou componentes discretos
(resistores, capacitores, diodos, transistores e amplificadores) por equações
de circuito: isso não equivale a emular o comportamento digital e analógico
de um chip histórico, que requer seu próprio modelo e validação.

Para o **Mega Drive**, separar dois objetivos: *emulação fiel* reproduz o
conjunto sonoro do sistema — YM2612 (ou YM3438, conforme revisão) e PSG
SN76489 —, respeitando canais, relógios, DAC e modo PCM; *emulação expandida*
usa essas arquiteturas como ponto de partida, mas permite mais vozes, novos
algoritmos, modulação entre motores ou parâmetros fora do hardware. A segunda
deve ser identificada como expansão, não apresentada como reprodução fiel.
Aplicar a mesma distinção ao SID, NES/2A03, TIA, POKEY, SN76489 e AY-3-8910: primeiro documentar
o comportamento reproduzido; depois expor extensões como camada opcional.

No **Commodore Amiga**, Paula fornece quatro canais de áudio DMA, com amostras
de 8 bits e pares de canais encaminhados aos lados esquerdo/direito. Isso
inspira um motor de reprodução de amostras com limites de canal, período,
volume e pan característicos; não é uma arquitetura FM.
No **Super Nintendo**, o SPC700 é o processador que executa o programa de som
e o **S-DSP** produz oito vozes estéreo a partir de amostras comprimidas BRR,
com pitch, envelopes, ruído, modulação de pitch e eco FIR. Uma emulação fiel
precisa modelar também o compartilhamento de RAM e os tempos entre SPC700 e
DSP; uma versão expandida pode liberar vozes ou efeitos, identificando essa
mudança. Referências: [Amiga Hardware Reference Manual, áudio Paula](https://www.ikod.se/wp-content/uploads/2020/08/Amiga_Hardware_Reference_Manual_3rd_Edition.pdf),
[SNESdev S-SMP/S-DSP](https://snes.nesdev.org/wiki/S-SMP),
[documentação dos registros SPC700/DSP](https://www.snesmusic.org/files/spc700_documentation.html).

As fontes pesquisadas sugerem técnicas complementares: caos pode ser usado
diretamente na onda ou como controle, e uma proposta recente o utiliza para
construir terrenos lidos por ondas periódicas; waveshaping fracionário molda
uma portadora com função racional; modulação fractal soma cópias escaladas em
múltiplas escalas; modelos de Weierstrass/Riemann podem ser amostrados como
onda. Isso fornece possibilidades de síntese e controle, não garantias de
musicalidade. Cada rota deve ser congelável, reproduzível pela seed e
comparável em bypass, intensidade fraca e intensa.

Referências: [Bytebeat (Viznut)](https://viznut.fi/texts-en/bytebeat_algorithmic_symphonies.html),
[Floatbeat (ossia score)](https://ossia.io/score-docs/reference/languages/bytebeat.html),
[osciladores caóticos (ICMC)](https://purehost.bath.ac.uk/ws/portalfiles/portal/665673/fractal_rev.html),
[síntese de sistemas dinâmicos](https://www.tandfonline.com/doi/full/10.1080/09298215.2026.2634043),
[caos como wave terrain](https://www.ecp.ep.liu.se/index.php/avs/article/view/1583),
[waveshaping fracionário (AES)](https://aes.org/publications/elibrary/elibrary-page/?id=4475),
[modulação fractal (DAFx)](https://dafx.de/paper-archive/details/dY5dQjcd_lsCzlj7Z-VcDQ),
[síntese por modelos matemáticos (DAFx)](https://www.dafx.de/paper-archive/2004/P_339.PDF),
[famílias FM e detalhes de chips (ymfm/MAME)](https://github.com/mamedev/mame/blob/master/3rdparty/ymfm/GeneralInfo.md).

Ordem proposta: (1) caos lento modulando índice FM e posição de wavetable;
(2) tabela criada por iteração fractal, com leitura periódica; (3) controle
caótico de densidade/posição/tamanho granular; (4) interpretação desses
descritores em eventos de altura, duração, ritmo, intensidade e forma; (5)
protótipo de circuito eletrônico virtual isolado, depois comparação com
modelos SID/NES. Cada motor precisa declarar custo, estado, taxa, faixa,
latência e política antialiasing antes de entrar no callback de áudio.

### Combinar Bytebeat, Floatbeat, sínteses e eletrônica

Pedido do autor: pesquisar síntese matemática contemporânea junto a SID/NES,
FM, granular, wavetable, CMOS e circuitos de componentes eletrônicos. Tratar
cada uma como arquitetura ou processo que pode ocupar uma etapa do fluxo,
em vez de escolher uma família e excluir as outras.

| Nível | Fonte → destino | Exemplo de uso combinado |
| --- | --- | --- |
| Amostras | Bytebeat/Floatbeat → waveshaper → quantizador | expressão vira áudio, recebe uma curva e é comparada em 8/16 bits |
| Oscilador | caos/fractal → fase, frequência ou wavetable | trajetória irregular alimenta leitura periódica para manter altura controlável |
| Timbre | FM, wavetable, granular, pulso/ruído NES ou SID → mixer | motores diferentes ocupam stems, com envelopes e limites próprios |
| Controle | equação ou sistema dinâmico → parâmetros | modula índice FM, largura de pulso, filtro, ganho, posição/tamanho de grão |
| Eventos | função matemática → altura, duração, acento e pausa | saída simbólica guiada por escala, métrica, papel e regras de frase |
| Circuito | modelo de componentes → oscilador, filtro e realimentação | modelo eletrônico produz o som; gestos alteram componentes nomeados |
| Forma | processo fractal/caótico → decisões por seção | repetição, rarefação, ápice e resposta evoluem em escala de frase |

Uma função caótica pode gerar amostras, modular um parâmetro lento ou propor
eventos. Os resultados e riscos são diferentes. Um caminho possível é
**mapa caótico → posição de wavetable → FM leve → envelope musical → stem**,
com seed, faixa e taxa de controle explícitas. Outro é
**texto/desenho → eventos → vozes NES/SID virtuais → filtro**. Modelar um
circuito CMOS ou discreto por equações não equivale a emular um chip histórico;
são níveis de modelo diferentes e cada um precisa de validação própria.

Os trabalhos consultados demonstram técnicas complementares: sistemas
caóticos podem atuar na própria onda ou como controle; uma proposta recente
os usa para construir terrenos lidos por formas periódicas; waveshaping
fracionário molda uma portadora com função racional; modulação fractal soma
cópias escaladas em múltiplas escalas; modelos de Weierstrass/Riemann podem
ser amostrados como onda. Cada rota deve ser reproduzível pela seed,
congelável e comparável em bypass, intensidade fraca e intensa.

Referências técnicas: [Bytebeat (Viznut)](https://viznut.fi/texts-en/bytebeat_algorithmic_symphonies.html),
[Floatbeat (ossia score)](https://ossia.io/score-docs/reference/languages/bytebeat.html),
[osciladores caóticos (ICMC)](https://purehost.bath.ac.uk/ws/portalfiles/portal/665673/fractal_rev.html),
[síntese de sistemas dinâmicos](https://www.tandfonline.com/doi/full/10.1080/09298215.2026.2634043),
[caos como wave terrain](https://www.ecp.ep.liu.se/index.php/avs/article/view/1583),
[waveshaping fracionário (AES)](https://aes.org/publications/elibrary/elibrary-page/?id=4475),
[modulação fractal (DAFx)](https://dafx.de/paper-archive/details/dY5dQjcd_lsCzlj7Z-VcDQ),
[síntese por modelos matemáticos (DAFx)](https://www.dafx.de/paper-archive/2004/P_339.PDF),
[famílias FM (ymfm/MAME)](https://github.com/mamedev/mame/blob/master/3rdparty/ymfm/GeneralInfo.md).

Ordem de protótipo: (1) caos lento modulando índice FM e posição de
wavetable; (2) tabela gerada por iteração fractal e leitura periódica;
(3) controle caótico de densidade, posição e tamanho granular; (4) tradução
desses descritores em eventos musicais; (5) motor de circuito isolado e
comparação com modelos SID/NES. Antes de entrar no callback, cada motor declara
custo, estado, taxa, faixa, latência e proteção contra aliasing.

## Formas de composição a experimentar

### Catálogo de arquiteturas históricas e modos fiel/expandido

Direção do autor (2026-11-16): criar uma biblioteca de motores independentes
com controles comuns de transporte, eventos, dinâmica e stems, preservando
os parâmetros particulares de cada circuito/instrumento. Cada motor oferece
dois modos identificados: **fiel**, que respeita topologia, vozes, relógio,
faixas e limitações documentadas; **expandido**, que parte daquela identidade
e libera extensões de polifonia, modulação, frequência ou roteamento. O modo
expandido nunca deve ser confundido com emulação fiel.

| Arquitetura | Princípio sonoro | Experimento no Risco |
| --- | --- | --- |
| Atari 2600 TIA | dois geradores com divisores e padrões de saída/ruído | explorar afinação áspera, padrões e microvariações |
| Atari POKEY | quatro canais, divisores e ruído polinomial; combinações de canais | usar ruído estruturado como ritmo ou modulação |
| AY-3-8910 / YM2149 | três tons, ruído compartilhado e envelope | sequenciar/polirritmia por regras |
| TI SN76489 | três tons e um canal de ruído | composição austera com limites de voz explícitos |
| Commodore Amiga Paula | quatro canais DMA de amostras PCM, pares em estéreo | desenhar/transformar amostras e organizar células como tracker |
| Super Nintendo SPC700 + S-DSP | CPU de som + DSP de oito vozes BRR, envelopes, pitch modulation e eco | combinar eventos, amostras e espacialização; fidelidade inclui interação/temporização entre CPU e DSP |
| Yamaha YM3812 / OPL2 | FM de nove canais, dois operadores por canal | comparar operadores moduladores/carriers e percussão OPL |
| Yamaha DX7 | FM de seis operadores e 32 algoritmos | explorar grafos de modulação como timbre e como forma |
| Roland TB-303 | sintetizador monofônico, filtro ressonante, accent/slide e sequenciador | compor acentos e slides como gestos ligados à linha de baixo |
| Roland TR-808 | vozes de percussão por circuitos analógicos e sequenciador | tratar cada voz como modelo e os passos como objeto composicional |
| Casio CZ | DCO, DCW por phase distortion e DCA | usar uma curva matemática para criar/animar o contorno espectral |
| Fairlight CMI | sampling, vozes digitais, edição gráfica e sequenciamento; especificações mudam por série | relacionar desenho de onda, amostra e composição por eventos |

Essas descrições são famílias, não contratos de emulação. TIA, POKEY e
SN76489 têm diferenças de chip/relógio por sistema; o Fairlight varia entre
séries; o Mega Drive combina YM2612/YM3438 com PSG SN76489. Confirmar revisão,
relógio e comportamento antes de implementar fidelidade.

**Cruzamentos composicionais propostos pelo autor:** processo estocástico →
ruído do POKEY; série dodecafônica → três vozes AY; fractal → parâmetros FM;
texto/desenho → pontos de leitura de amostras Paula; autômato celular → passos
da TR-808. Para cada cruzamento, documentar o mapeamento, o que permanece
reconhecível da arquitetura e o que é expansão. Comparar mesma seed e eventos
nos modos fiel, expandido e bypass.

**Ordem sugerida:** começar por POKEY/TIA/AY para explorar geração de eventos
com poucos recursos; depois Paula e S-DSP para trabalhar com amostras; seguir
para OPL2/DX7 e CZ para síntese por modulação; por fim modelar TB-303/TR-808
por circuitos e integração gestual. Fairlight CMI amplia a pesquisa para
workstation, edição gráfica e sequenciamento sample-based.

Referências técnicas de entrada: [TIA, Atari 2600](https://www.atariarchives.org/deli/appendix_1.php),
[POKEY, Atari](https://www.atarimax.com/jindroush.atari.org/achip.html),
[Roland TR-808, especificações e manual](https://support.roland.com/hc/en-us/articles/201963539-TR-808-Technical-Specifications),
[circuitos TR-808](https://el34world.com/charts/Schematics/files/Roland/Roland_tr_808_service_manual.pdf),
[Casio CZ, manual de operação](https://www.manualslib.com/manual/4126087/Casio-Cosmo-Synthesizer-Cz-1000.html),
[YM3812/OPL2, ymfm/MAME](https://github.com/mamedev/mame/blob/master/3rdparty/ymfm/GeneralInfo.md),
[Fairlight CMI, manual de operação](https://www.outofphase.fr/wp-content/uploads/2023/04/cmi_users_manual_v3_09.pdf).
Antes de incorporar emuladores ou modelos de terceiros, verificar licença,
proveniência e compatibilidade com AGPL; referência técnica não autoriza
copiar código.

- **Célula e retorno:** uma função curta vira motivo recorrente; cada retorno
  pode repetir exatamente ou escolher uma variação registrada.
- **Acumulação:** cada REC adiciona um stem ou camada e a música cresce até o
  músico parar, retirar ou silenciar uma ocorrência.
- **Chamada e resposta:** gesto ou função do stem A convoca uma resposta do B;
  pausas e silêncios também contam como eventos.
- **Transformação:** repetir o mesmo material mudando uma dimensão de cada
  vez, para que a transformação seja percebida.
- **Ramificação:** gerar alternativas a partir da mesma função ancestral e
  selecionar, alternar ou sobrepor ramos.
- **Contaminação:** abrir/fechar rotas entre stems durante a performance, com
  intensidade gradual e parâmetros limitados.
- **Varredura textual:** o dial percorre um texto e a posição/caráter ativa
  funções ou eventos; a mesma passagem pode ser retomada em outro andamento.
- **Forma aberta:** não definir número fixo de compassos; regras de parada,
  densidade ou gesto do músico concluem ou suspendem a geração.

## Tempo, gravação e retorno

Manter um contador de amostras `t` contínuo para os geradores Bytebeat e um
transporte musical em BPM/compassos para a composição. São relógios ligados
por uma conversão, não a mesma variável. Um evento gravado deve ter tempo
musical e referência de amostras suficiente para reprodução determinística.

**REC** deve primeiro registrar a performance algorítmica: funções, alterações,
gestos, parâmetros, desenho de onda, stem e instante. Isso não é a mesma coisa
que gravar PCM. A gravação de áudio pode ser adicionada como outro recurso,
sem substituir o log editável. Para reexecutar uma função, a performance
recupera sua versão e seus parâmetros; uma nova interpretação gera outra
versão ligada à anterior.

## Estado do protótipo e sequência de pesquisa

Já há uma maquete JUCE/C++ com bytebeat direto, geração determinística de
frases a partir de seed, texto ou desenho, banco/grade, BPM, master, Parar e
um botão REC para frases. Células generativas conservam 128 pontos de onda;
o formato atual é V41 e seleção de célula mostra esse snapshot. O texto entra como
UTF-8 na composição e como bytes UTF-8 para um seed determinístico: não há
leitura fonética, sintática ou semântica. Expressões do Bytebeat direto agora
podem ser registradas e sequenciadas; frases fórmula→eventos guardam a fórmula,
os eventos e a regra composicional; ainda não há gravação PCM. A seleção
8/16-bit já quantiza as vozes por notas e fica salva no arquivo da música;
Bytebeat direto permanece em 8 bits. A grade tem cinco stems persistidos com
volume, pan, mute e solo, que compartilham MASTER e saída estéreo. Os canais
Ritmo e Textura ampliam o núcleo melodia/harmonia/baixo; arquivos antigos
preservam os três stems originais e recebem os dois novos em `STEM_EXTRAS`;
saídas
físicas separadas ainda não existem. O grafo de influência, Floatbeat e síntese híbrida
controlável ainda são propostas. O perfil de trio chiptune próprio já pode
ser selecionado; o orçamento de três vozes ainda é local a cada célula. O
quadro único de texto/desenho escolhe a entrada ativa para som e célula, sem
misturar o rascunho inativo.

Ordem de experimentação recomendada:

1. Escutar se uma frase e seu desenho de onda são reconhecíveis ao voltar na
   mesma performance; verificar REC e andamento.
2. Comparar a mesma função com quantização 8/16-bit, sem trocar outras
   variáveis; depois comparar timbres com a mesma resolução.
3. Fazer gravação de uma expressão bytebeat como evento persistente, com seu
   código, seed, contador inicial, preview de onda e instante na grade.
4. Acrescentar nível, mute e solo por stem; medir margem de nível ao somar
   células sobrepostas antes de ligar qualquer influência entre stems.
5. Adicionar uma única rota de influência limitada, gravando cada resultado
   como descendente reversível; avaliar musicalmente antes de multiplicar
   operadores ou rotas.
6. Testar leituras textuais por forma e ritmo antes de interpretação
   semântica, sempre comparando versões com a mesma fonte.

### Primeira tradução de fórmula em eventos — 2026-11-16

O botão **Fórmula → notas** materializa uma terceira interpretação da função:
além de produzir amostras diretamente ou servir de onda para notas já
compostas, a expressão gera uma frase musical tocada pelo motor de notas.
`src/core/FormulaComposition.*` lê um byte da expressão a cada semicolcheia;
os quatro bits baixos selecionam grau da escala e oitava, os dois bits
seguintes determinam gate de 1–4 passos e os dois superiores determinam
acento/energia. O byte zero representa pausa. A métrica acentua o primeiro
tempo e o início dos grupos secundários: 5/4 em 3+2, 6/4 em 3+3 e 7/4 em
2+2+3; 4/4 recebe apoio no terceiro tempo e 3/4 no terceiro. Outros tempos
inteiros recebem acento leve. BPM e compasso vêm dos controles atuais;
a frase ocupa até quatro compassos, com limite rígido de 64 eventos por
snapshot.

**Guardar frase** transforma a pré-escuta em célula persistente: guarda a
expressão de origem, os eventos materializados, a escala inicial, o movimento
da máquina e o BPM de geração; a célula entra ao fim da grade, no stem
selecionado. O formato de música V41 registra eventos, MASTER, os presets de
escala, tônica, agrupamento métrico, papéis das pistas, máscara de seções e o
blueprint V5; continua aceitando os formatos anteriores. Na reprodução,
a frase preservada é ajustada ao BPM atual sem recompilar a expressão. Alterar fórmula, escala, tônica ou movimento depois
de guardar não reescreve a célula existente; gere outra para criar uma nova
versão editável.

**Resposta entre stems (V22, seção ativa em V25):** o seletor permite desligar
ou escolher uma das três rotas (1→2, 2→3, 3→1) e uma transposição (quarta
acima/abaixo ou oitava abaixo). Uma célula integral com eventos procura sua
primeira barra ativa que permita encaixar uma resposta de um compasso. Copia o
ritmo e as durações para o stem seguinte, uma barra depois; espelha o contorno
de alturas pela média geométrica das frequências e aplica o intervalo escolhido
a 72% do ganho original. A posição respeita a máscara de seções da célula:
desligar A permite que B ou outra seção ativa origine a resposta. O playback
limita a cópia à duração tocável e pré-calcula o material fora do callback;
cada bloco convoca no máximo
uma resposta e ela não vira nova chamada. Bytebeat cru, recortes e células sem
espaço para encaixar a resposta não participam. A direção e o intervalo são
salvos; projetos V1–V20 abrem com a resposta desligada, e V21 mantém o
mapeamento antigo Stem 1→2, quarta abaixo.

A escala de partida pode ser escolhida entre 27 presets, incluindo modos de
jazz (bebop, alterada, diminutas e lídio dominante), klezmer (Freygish/Ahava
Rabbah e Misheberakh), maior harmônica, menor napolitana, menor húngara, dupla
harmônica, aumentada, persa, Prometheus, tons inteiros e cromática; a tônica
inicial é escolhida no controle Tom (Dó é o padrão). Os 16 graus do byte cobrem
exatamente três oitavas, da tônica à tônica final; os graus
intermediários são distribuídos uniformemente. A cada barra, o byte calculado
no início do compasso guia uma
operação da máquina: **seguir** faz rotação ou alteração pequena, **contrapor**
espelha os intervalos e **transfigurar** interpola para outro preset dentro da
mesma família selecionada (básica, Jazz, Klezmer ou Cor), preservando o campo
de intenção escolhido.
Assim a mesma função controla nota e desenvolvimento harmônico, com escolha
de escala e comportamento visíveis na interface. O adaptador em
`src/core/FormulaScaleMachine.*` usa cents e conserva desvios microtonais na
frequência gerada.

Os presets **Jazz** incluem bebop maior/dominante, dominante alterada, duas
simétricas diminutas e lídio dominante, como material para exploração de
improvisação; o seletor não faz análise automática de acordes. No grupo
**Klezmer**, Freygish (também chamada Ahava Rabbah) e Misheberakh (descrita em
estudos como Dorian com quarta elevada) representam coleções de alturas. Os
nomes e transliterações variam. Uma coleção de alturas não representa sozinha
cadências, ornamentação, fraseado ou prática cultural; os presets usam alturas
em temperamento igual. Referências: [Berklee Online — escalas para improvisação
de jazz](https://online.berklee.edu/takenote/jazz-improvisation-10-scales/) e
[Music Theory Online — estudo de corpus dos modos klezmer](https://www.mtosmt.org/issues/mto.25.31.3/mto.25.31.3.malin.php).

A pré-escuta não modifica banco, grade ou fórmula fonte; **Guardar frase**
materializa uma célula e a insere no fim da grade, com fórmula, escala,
movimento, tônica e eventos preservados no formato V19. O resultado é determinístico
para a expressão, BPM e métrica. É uma adaptação
autoral dos conceitos registrados no Atlas e adaptada da máquina de escalas do
Trioio, com proveniência e licença registradas em `CREDITOS_E_LICENCAS.md`;
não há dependência entre instrumentos nem cópia do arquivo de escalas do
Trioio. A escolha de escala inicial e comportamento já é visível, mas ainda
faltam comparação auditiva e formas de limitar/selecionar registro e densidade.
Os bits hoje têm papéis fixos,
então a saída é uma prova de tradução composicional, não um arranjador
completo.
