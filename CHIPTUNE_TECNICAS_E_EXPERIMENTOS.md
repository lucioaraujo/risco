# Risco — técnicas chiptune e experimentos de composição

Pesquisa-criação iniciada em 2026-11-13. Este documento distingue fatos sobre
sistemas históricos de propostas próprias para o Risco. As fontes abaixo
informam o projeto; nenhum código, timbre, música ou preset de terceiros foi
incorporado.

## O que interessa ao instrumento

Chiptune oferece **restrições programáveis** que podem atuar na forma musical.
Um limite de vozes, por exemplo, obriga a escolher entre sobrepor notas,
alterná-las rapidamente ou interromper uma linha. Essa escolha pode ser
gravada como acontecimento da performance, influenciada por texto e desenho e
revisitada como célula.

As referências históricas não formam um único som. A APU do NES combina duas
vozes pulsadas, triângulo, ruído e DMC; o Game Boy tem canais pulsados, onda
programável e ruído; o SID apresenta três vozes com diferentes ondas, envelope
e filtro; o YM2612 pertence à família FM. Cada arquitetura sugere um tipo de
orçamento e articulação musical distinto. Fontes técnicas:
[NES APU](https://www.nesdev.org/wiki/APU_basics),
[Pan Docs / áudio](https://github.com/gbdev/pandocs/blob/master/src/Audio_details.md),
[datasheet SID 6581](https://speechchips.com/documents/sid-6581-2.pdf),
[Furnace / YM2612](https://codigo.otomatalabs.net/otomatalabs/furnace/src/commit/4079f7a8c3b383918308f89de1b46b9faa4964ec/doc/7-systems/ym2612.md).

## Técnicas a traduzir em operações do Risco

| Técnica | Referência documentada | Uso composicional proposto |
| --- | --- | --- |
| Arpejo rápido | [LSDj, manual](https://www.littlesounddj.com/latest/documentation/LSDj_5_3_7.pdf); [efeitos Furnace](https://github.com/tildearrow/furnace/blob/master/src/engine/engine.cpp) | Fazer uma voz sugerir acorde por alternância de alturas. A função ou o desenho define ordem, velocidade e notas omitidas. |
| Largura de pulso | [NES APU Pulse](https://www.nesdev.org/wiki/APU_Pulse); [Pan Docs](https://github.com/gbdev/pandocs/blob/master/src/Audio_Registers.md) | Modificar o timbre durante uma frase; texto ou gesto controla a sequência de larguras. |
| Onda programável | [Pan Docs / áudio](https://github.com/gbdev/pandocs/blob/master/src/Audio_details.md) | Amostrar o contorno de uma letra ou desenho numa tabela curta, preservando também a fonte gráfica. |
| Ruído como voz | [NES APU](https://www.nesdev.org/wiki/APU_basics); [Pan Docs](https://github.com/gbdev/pandocs/blob/master/src/Audio_details.md) | Pontuação e riscos geram acentos, ataques e pausas, sem obrigar cada símbolo a virar nota. |
| Tabelas de automação | [manual LSDj](https://www.littlesounddj.com/latest/documentation/LSDj_5_3_7.pdf); [Furnace](https://github.com/tildearrow/furnace) | Uma célula guarda também um percurso de timbre, altura e dinâmica, reexecutável ou mutável. |
| Mistura não linear | [NES APU Mixer](https://www.nesdev.org/wiki/APU_Mixer) | Investigar como a interação entre vozes muda o resultado; começar com mistura segura e fazer comparação auditiva antes de modelar o circuito. |
| Vozes e filtro SID | [datasheet SID 6581](https://speechchips.com/documents/sid-6581-2.pdf); [reSID](https://github.com/libsidplayfp/resid) | Estudar forma, envelope, filtro e interação entre três vozes como um perfil composicional separado. |
| FM em canais limitados | [documentação Furnace / YM2612](https://codigo.otomatalabs.net/otomatalabs/furnace/src/commit/4079f7a8c3b383918308f89de1b46b9faa4964ec/doc/7-systems/ym2612.md) | Comparar uma mesma célula em timbre FM com versões pulso/triângulo, mantendo eventos e função de origem. |

“Risco inspirado em NES/Game Boy/SID” deve designar **regras ou paletas
explícitas**. Emulação fiel só pode ser nomeada depois de implementar e
verificar temporização, registradores, envelopes, mistura e diferenças entre
modelos. A camada inicial do Risco não promete equivalência ao hardware.

## Primeira fatia musical recomendada

Uma cena curta de quatro compassos, com até três vozes simultâneas:

1. **Voz pulso:** lê a função ou o texto como motivo; largura de pulso muda
   em poucos pontos predefinidos.
2. **Voz grave triângulo:** sustenta ou responde ao motivo, com silêncio
   intencional quando a voz principal precisa de espaço.
3. **Voz ruído:** marca pontuação, cruzamento do traço e transições de célula.
4. **Arpejo de uma voz:** quando a harmonia pede mais notas do que o orçamento
   permite, alternar as alturas e registrar a decisão.

Usar o mesmo seed, texto, BPM e grade para comparar esse arranjo com a síntese
atual de seno quantizado. Variar uma dimensão por vez: timbre, orçamento de
vozes, depois política de arpejo. A preferência musical será decidida por
escuta, não pelo fato de um perfil imitar um sistema conhecido.

## Possíveis invenções específicas do Risco

- **Orçamento que compõe:** o limite de vozes não apenas corta notas; gera
  opções registráveis de arpejo, deslocamento, substituição ou silêncio. O
  músico escolhe a política por stem ou deixa a máquina variar dentro de
  limites.
- **Glifo como tabela de onda:** o desenho armazenado pode ser reamostrado
  como onda periódica curta. A transformação precisa guardar escala, direção,
  filtro e versão, pois o contorno visual não é automaticamente uma onda
  musical útil.
- **Contaminação entre stems:** uma voz transfere ritmo de arpejo ou percurso
  de largura de pulso a outra; a função descendente fica no banco com origem
  e intensidade da influência.
- **Retorno com outra arquitetura:** a mesma célula volta em voz pulsada,
  Bytebeat ou FM. O banco preserva a função/eventos originais e registra a
  nova realização sonora e seu desenho de onda.
- **Escrita performática:** o dial de texto pode disparar mudanças de canal,
  efeito ou orçamento; o REC grava essas mudanças como partitura executável.

Essas são hipóteses de pesquisa. Arpejos, ondas programáveis, trackers e
síntese por chips têm antecedentes claros nas fontes acima. A possível
originalidade do Risco dependerá da realização e da relação musical entre
escrita, funções, memória, desenho e performance.

## Limites práticos para implementar

- Separar evento musical, voz osciladora e perfil de restrições do chip.
- Manter cada voz limitada em CPU/memória e toda mudança de estado fora de
  alocação no callback de áudio.
- Armazenar em cada célula a fonte, parâmetros de timbre, resolução,
  derivação, desenho e versão; o preview visual não substitui a função sonora.
- O primeiro experimento agora usa vozes pulso/triângulo/ruído próprias e
  alternância de até três notas graves/harmônicas por célula. Avaliar licença
  e fidelidade antes de incorporar
  qualquer emulador de terceiros.
- Fazer escuta A/B da mesma célula com o motor atual e o novo perfil; só
  depois ampliar a lista de chips e efeitos.

Estado do código em 2026-11-13: a maquete tem Bytebeat direto, síntese por
notas com quantização selecionável 8/16-bit, geração de frase, banco/grade e
REC para frases. Um perfil próprio de trio pulso/triângulo/ruído e arpejo
simples está disponível na interface e no renderizador offline; o formato V6
salva sua escolha. O limite de vozes vale por célula e ainda requer escuta.
Ainda não possui orçamento global entre células, wavetable extraída de glifo
ou emulação histórica.
