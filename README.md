# ESTaxiRoute

Plugin para o EuroScope que guarda a **rota de táxi** de cada tráfego e a
**desenha na ASR**.

- Você anota o táxi direto no **TAG** ou numa coluna da **Departure List** (ou
  qualquer outra lista) — clique, digite `A B C 27L`, Enter.
- A rota fica salva para aquele callsign e aparece no tag/lista.
- Na ASR, o plugin desenha o caminho a partir da posição atual da aeronave,
  seguindo as taxiways na ordem que você passou, até a pista/ponto final.

## Instalação

1. Copie `ESTaxiRoute.dll` e `ESTaxiRoute.txt` (da pasta `config/`) para uma
   mesma pasta (ex.: a pasta do seu pacote de setor, em `Plugins/`).
2. No EuroScope: **OTHER SET → Plug-ins… → Load** e escolha a DLL.
3. Na mesma janela, em **Allowed to draw on types**, selecione `ESTaxiRoute`
   e clique em **>>** para cada ASR onde quer ver o desenho (ex.: GND/TWR).

### Colocar no TAG

**OTHER SET → TAG Editor**, escolha o tag (ex.: *Ground / Tower*), adicione um
item:

- **Item type:** `ESTaxiRoute / Taxi route`
- **Left button:** `ESTaxiRoute / Edit taxi route`
- (opcional) **Right button:** `ESTaxiRoute / Release/lock stop bar` ou
  `ESTaxiRoute / Clear taxi route`

Também existe `Taxi route flag (TX)`, que mostra só `TX` quando há rota —
útil em tags compactos.

### Colocar na Departure List (a lista de cima)

Clique no **S** (settings) no canto da Departure List → **Add Item**:

- **Header name:** `TAXI`
- **Width:** 15
- **Tag item type:** `ESTaxiRoute / Taxi route`
- **Mouse button left:** `ESTaxiRoute / Edit taxi route`

### Colocar no tag do vSMR

O vSMR só mostra os itens dele, não os de outros plugins. Mas o item
**`uk_stand`** do vSMR mostra a **anotação 3 da flight strip**, então o
ESTaxiRoute pode escrever a rota ali:

1. No EuroScope, digite `.taxi vsmr on` (fica salvo).
2. No `vSMR_Profiles.json`, acrescente `"uk_stand"` à definição do tag. Por
   exemplo, uma terceira linha no tag de partida:

```json
"departure": {
    "definition": [
        ["callsign", "dest", "groundstatus"],
        ["sctype", "asid", "deprwy"],
        ["uk_stand"]
    ],
```

3. Recarregue o perfil do vSMR (ou reinicie o EuroScope).

Observações:
- Sem rota, o vSMR mostra `NoGate` nesse item. Isso vem do próprio vSMR.
- Clicar no item no tag do vSMR não abre o editor, porque o vSMR não repassa
  esse clique. Edite pela Departure List, pelo tag do EuroScope ou com
  `.taxi <rota>`.
- A anotação 3 passa a ser só da rota. Não use junto com outro plugin que
  grave nela (ex.: UK Controller Plugin). Como a anotação vai junto com a
  strip, quem recebe a strip e também usa `.taxi vsmr on` vê a mesma rota.
- Para o desenho aparecer na tela do vSMR, em **Plug-ins… → Allowed to draw
  on types** libere o `ESTaxiRoute` também no tipo de display do vSMR.

**Não apareceu no tag do vSMR?**
- Se nem `NoGate` aparece, o `uk_stand` não está na definição do tag que está
  sendo usado. Confira se editou o `vSMR_Profiles.json` que o vSMR carrega (na
  pasta da `vSMR.dll`), no **perfil ativo** e no tipo de tag certo
  (`departure`, `arrival`...), e recarregue o perfil.
- Se aparece `NoGate`, a anotação está vazia: só controladores que estão
  trackeando o tráfego (ou com ele sem controle) conseguem gravar a anotação. O
  plugin avisa quando o EuroScope recusa.
- Ao ligar `.taxi vsmr on`, as rotas já existentes são gravadas na anotação.

## Uso

Clique no item → abre uma caixa de texto → digite a rota → Enter. Deixar
vazio apaga a rota. Separadores aceitos: espaço, ponto ou vírgula
(`A B C 27L`, `A.B.C.27L`).

Como a rota é interpretada no desenho:

| Token | Significado |
|---|---|
| Nome de taxiway (`A`, `B1`…) | segue por essa taxiway até cruzar com a próxima |
| Pista (`10L`, `09`, `RWY27L`) no **fim** | termina no lado da cabeceira informada: se a taxiway cruza a pista, para ~70 m antes do eixo no cruzamento mais perto dessa cabeceira; se é paralela (ex.: `A 10L`), vai até o ponto da taxiway mais perto da cabeceira |
| Pista no **meio** (`A 09 B`) | cruzamento de pista, o desenho continua |
| Ponto (`POINT` do arquivo, ex.: `H1`, stand) | vai até o ponto |
| Barra de parada (`STOPBAR` do arquivo, ex.: `SB-G`) no **fim** | a rota termina nessa barra |
| Barra de parada no **meio** | cruzamento dessa barra liberado (veja abaixo) |
| Qualquer outra coisa (`HS`, `VIA`…) | aparece no tag, ignorado no desenho |

O tag mostra até 15 caracteres (o limite do EuroScope); rotas maiores aparecem
com `+` no fim, mas ficam salvas inteiras (veja abrindo o editor).

**Pista do plano de voo:** se a rota termina numa taxiway (ex.: `H A G`) e o
tráfego está no aeroporto de origem, o desenho vai até a pista de decolagem
do plano de voo (a mesma do tag/lista), como se você tivesse digitado
`H A G 10L`. O texto da rota não muda. Desligue com `.taxi autorwy off`.

**Liberar barra com um clique:** a função `Release/lock stop bar` libera a
barra onde a rota está parada (põe o nome dela no meio da rota). Clicando de
novo quando não há barra para liberar, trava todas as barras liberadas. Pelo
comando: `.taxi release` / `.taxi lock`. Uma barra no **fim** da rota é o
limite que você deu e não é liberada pelo clique.

A rota é apagada automaticamente quando o tráfego desconecta ou passa de
80 kt (decolou). O desenho só aparece com a aeronave abaixo de 50 kt, a até
10 NM de um aeroporto cadastrado no arquivo. A rota do tráfego selecionado
(ASEL) é desenhada mais grossa.

### Barras de parada (stop bars)

Barras cadastradas no arquivo (`STOPBAR`) funcionam como um limite: o desenho
da rota **para na primeira barra que encontrar** pelo caminho, e a barra
aparece em **vermelho**.

- `H A G SB-G10L/N`: barra no **fim** = taxiar **até** essa barra. A rota
  termina nela.
- `A G 10L`: se houver uma barra em G antes da pista, a rota para nela.
- `G SB-G10L/N SB-G10L/S C`: barras no **meio** da rota = cruzamento
  liberado. A rota cruza a pista e segue por C.

Uma barra que está embaixo da aeronave (a menos de 5 m) não corta a rota.
`.taxi bars off` desliga esse limite.

### Comandos

Digite na linha de comando do EuroScope:

| Comando | Ação |
|---|---|
| `.taxi A B C 27L` | define a rota do ASEL |
| `.taxi clear` | apaga a rota do ASEL |
| `.taxi show all` / `asel` / `off` | desenha todas / só a do ASEL / nenhuma |
| `.taxi color 255 200 0` | cor do desenho |
| `.taxi annot 7` / `off` | compartilha a rota com outros controladores (ver abaixo) |
| `.taxi bars on` / `off` | rotas param / não param nas barras de parada |
| `.taxi release` / `.taxi lock` | libera a barra onde a rota do ASEL está parada / trava todas |
| `.taxi autorwy on` / `off` | completa rotas terminadas em taxiway com a pista do plano de voo |
| `.taxi vsmr on` / `off` | grava a rota na anotação 3 para o item `uk_stand` do vSMR |
| `.taxi debug` | mostra as taxiways, pontos e barras carregados (para conferir o arquivo) |
| `.taxi reload` | recarrega `ESTaxiRoute.txt` |
| `.taxi rec SBGR A` | começa a gravar a taxiway A clicando no radar |
| `.taxi recpt SBGR H1` | grava um ponto com um clique |
| `.taxi recbar SBGR SB-G` | grava uma barra de parada com dois cliques (um de cada lado da taxiway) |
| `.taxi undo` / `save` / `cancel` | desfaz o último clique / salva / cancela a gravação |
| `.taxi status` | diagnóstico: versão, arquivo carregado, telas com permissão de desenho e o caminho da rota do ASEL |
| `.taxi help` | lista os comandos |

`show`, `color`, `annot`, `bars`, `autorwy` e `vsmr` ficam salvos nas configurações do EuroScope.

### Compartilhar a rota com outros controladores

Por padrão a rota fica só no seu EuroScope. Com `.taxi annot <0-8>` o plugin
também grava a rota numa **anotação da flight strip** (como `TX:A B C 27L`),
que vai junto quando a strip é passada; quem também usa o plugin com o mesmo
índice vê a rota. Escolha um índice que nenhum outro plugin seu use — o
ESTaxiRoute nunca sobrescreve uma anotação que não comece com `TX:`.

## Cadastro das taxiways (`ESTaxiRoute.txt`)

O setor (.sct) não traz as linhas centrais das taxiways com nome, então o
plugin usa um arquivo próprio. As **pistas** vêm do setor automaticamente.

```
AIRPORT SBGR
TWY A  S023.25.40.000 W046.28.30.000 S023.25.40.000 W046.27.30.000
TWY B  -23.4278 -46.4750  -23.4300 -46.4750
POINT H1 S023.25.30.000 W046.27.40.000
STOPBAR SB-G S023.25.45.000 W046.28.31.000 S023.25.45.000 W046.28.29.000
```

- `STOPBAR` é uma linha que **atravessa** a taxiway (dois pontos, um de cada
  lado). Dê nomes que não sejam iguais aos de taxiways, ex.: `SB-G`, `G-10L`.

- Coordenadas no formato do EuroScope ou decimal (latitude, longitude).
- Uma taxiway pode ter várias linhas `TWY` com o mesmo nome.
- Para o desenho seguir bem, as taxiways que se conectam devem se cruzar (ou
  quase — o plugin liga pelo ponto mais próximo).

**Jeito fácil:** com a ASR do aeroporto aberta, digite `.taxi rec SBGR A` e
clique ao longo da linha central da taxiway A (um clique em cada curva e
cruzamento). Termine com `.taxi save`. O plugin acrescenta a linha no
arquivo e recarrega. Durante a gravação o clique esquerdo no radar fica
reservado para o plugin. Use `.taxi debug` para conferir o resultado.

Sem o arquivo o plugin funciona normalmente no tag e na lista; só não desenha.

## Compilar

Precisa do Visual Studio (com C++) e CMake. O EuroScope é 32 bits.

1. Copie `EuroScopePlugIn.h` e `EuroScopePlugInDll.lib` da pasta
   `PlugInEnvironment` da sua instalação do EuroScope para `sdk/`.
2. Compile:

```
cmake -S . -B build -A Win32
cmake --build build --config Release
```

A DLL sai em `build/Release/ESTaxiRoute.dll`.

Os testes da lógica de rota (sem EuroScope) rodam em qualquer sistema:

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```
