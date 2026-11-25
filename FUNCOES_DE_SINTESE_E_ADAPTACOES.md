# Risco — funções de síntese sonora e adaptações

Estudo de 2026-11-13. Direção artística: Lúcio de Araújo. As expressões abaixo
são descrições matemáticas e propostas de implementação próprias, não presets
copiados de outras obras. [Créditos e direitos](CREDITOS_E_LICENCAS.md).

## Três relógios e duas saídas

- `n`: contador de amostras de áudio, com taxa `Fs`; no Bytebeat direto atual,
  `t` avança em 8 kHz e a saída é `uint8` interpretada como PCM.
- `k`: passo de controle musical, sincronizado ao BPM ou a uma taxa menor;
  modifica parâmetros, escolhe células e registra a transformação.
- `s`: posição do gesto, texto ou desenho; sua varredura pode avançar no tempo
  musical sem confundir coordenada espacial com amostra de áudio.

`y[n]` pode ser quantizado depois do motor em 8 ou 16 bits. Isso é distinto da
largura dos inteiros usados para calcular o som. A taxa interna e a profundidade
da saída devem acompanhar cada fórmula arquivada. O repositório atual só guarda
expressões Bytebeat inteiras de até 128 caracteres; as famílias com estado ou
funções reais pedem um formato de receita tipada, com versão e parâmetros.

## Fórmulas que cabem no motor Bytebeat atual

O parser aceita `t`, inteiros, `+ - * / % & | ^ << >>` e parênteses. Estas são
propostas autorais simples para testar o mesmo contador contínuo:

| Função | Expressão de estudo | Experimento musical |
| --- | --- | --- |
| Rampa modular | `t*5` | Ouvir a frequência aparente e comparar o desenho em 8 kHz. |
| Pulso binário | `(t>>5)&128` | Alternar o deslocamento para deslocar ritmo e região espectral. |
| Degraus rítmicos | `(t*7)&(t>>6)` | Trocar o multiplicador a cada célula, conservando o mesmo `t`. |
| Combinação de camadas | `((t*5)&(t>>6))^((t*3)&(t>>9))` | Separar os dois termos em stems e medir o efeito da recombinação. |

Essas expressões são estudos novos do Risco, não alegações de originalidade
sobre as operações matemáticas. A taxa de 8 kHz e a redução aos oito bits
baixos podem produzir aliasing; comparar também com renderização filtrada e
saída de 16 bits antes de escolher um timbre. Divisão por zero no parser vale
zero, deslocamentos são reduzidos a 0–31 e a aritmética usa `uint32` modular.

## Próximos motores de síntese

| Família | Forma matemática e estado | Papel no Risco | Prioridade |
| --- | --- | --- | --- |
| Aditiva | `y[n]=Σ a_j sin(φ_j[n])`, `φ_j[n+1]=φ_j[n]+2π f_j/Fs` | Cada curva ou glifo fornece amplitudes de parciais; texto altera relações harmônicas. Necessita limite de parciais e antialiasing. | 2 |
| FM | `y[n]=sin(φ_c[n]+I sin(φ_m[n]))`; duas fases acumuladas | Gesto altera índice `I` e razão portadora/moduladora; guardar automação e limites de frequência. | 2 |
| Wavetable | `y[n]=W(φ[n])` com interpolação e tabelas de um ciclo | Desenho pode virar uma tabela de onda normalizada; guardar traço original e tabela derivada, com política de banda. | 1 |
| Waveshaping | `y[n]=f(x[n])`, por exemplo `tanh(g x[n])` | Texto/gesto mudam ganho `g` ou curva `f`; aplicar limite de saída e comparar aliasing. | 1 |
| Diferença / integral discreta | `d[n]=x[n]-x[n-1]`; `z[n]=(1-λ)z[n-1]+λx[n]` | Transições do desenho viram ataques; integrador com vazamento gera memória. Exige estado e reinício explícitos. | 1 |
| Corda por atraso | `y[n]=D[n]`; `D[n+L]=(D[n]+D[n-1])/2` em esquema de realimentação | Traço pode preparar a excitação de uma corda; limitar memória, ganho e polifonia. | 3 |
| Granular | `y[n]=Σ g_i[n] w_i[n]`, grãos com janela e posição próprias | Fragmentos de funções e desenhos reaparecem em nuvens; usar orçamento fixo de vozes. | 3 |
| Sistema logístico | `x[k+1]=r x[k](1-x[k])`, `0≤x≤1`, em taxa de controle | Varia densidade, acordes ou parâmetros de futuras funções; guardar seed, `r`, taxa e trajetória curta para repetição. | 2 |

As prioridades são decisão de projeto, não classificação científica. O primeiro
protótipo após Bytebeat deve usar **wavetable derivada do desenho** e
**waveshaping de fonte já existente**. Eles ligam diretamente as ideias de
inscrição e de onda, cabem numa interface de uma tela e permitem comparação
audível com o mesmo material. O mapa logístico deve comandar a composição em
`k`, antes de qualquer tentativa de transformar a recorrência diretamente em
PCM. Um motor de FM ou aditiva viria depois, com parâmetros e visualização de
espectro. Para derivadas e integrais, consultar também a seção correspondente
do [Atlas](../../RASGO_DOCUMENTATION/ATLAS_DE_REFERENCIA_RASGO(4).md).

## Contrato para a futura receita

Cada receita precisa registrar: `engine`, versão, equação ou algoritmo,
parâmetros e limites, taxa de áudio/controle, seed, política de reinício,
quantização, fonte de texto/desenho, autoria e licença, IDs dos pais e uma
prévia de onda. A avaliação deve ser limitada e sem alocação no callback;
compilar a receita e calcular tabelas fora dele. A célula da performance
referencia a receita e guarda sua versão para reproduzir o som posteriormente.
Uma alteração de parâmetro ao longo do tempo deve virar evento registrado, não
substituição silenciosa da receita antiga.

## Fontes primárias estudadas

- [CMU, síntese aditiva](https://www.cs.cmu.edu/~15322/book/ch03/02.html),
  [wavetable](https://www.cs.cmu.edu/~15322/book/ch03/04.html),
  [FM](https://www.cs.cmu.edu/~15322/book/ch06/05.html),
  [waveshaping](https://www.cs.cmu.edu/~15322/book/ch13/05.html) e
  [granular](https://www.cs.cmu.edu/~15322/book/ch10/03.html): modelos didáticos;
  nenhum código ou material sonoro foi copiado.
- [Karplus e Strong, artigo original de 1983](https://users.soe.ucsc.edu/~karplus/papers/digitar.pdf):
  antecedente da corda por atraso; PDF de artigo acadêmico, sem licença de
  reutilização do texto ou das figuras conferida.
- [MathWorld, mapa logístico](https://mathworld.wolfram.com/LogisticMap.html):
  referência para a recorrência; nenhuma implementação foi copiada.

Próximo experimento: criar um pequeno banco local de tabelas autorais geradas
dos traços existentes, comparar uma tabela com a expressão Bytebeat da mesma
célula, medir CPU e ouvir em 8/16 bits. Ainda não foi implementado.
