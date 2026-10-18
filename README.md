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
- (opcional) **Right button:** `ESTaxiRoute / Clear taxi route`

Também existe `Taxi route flag (TX)`, que mostra só `TX` quando há rota —
útil em tags compactos.

### Colocar na Departure List (a lista de cima)

Clique no **S** (settings) no canto da Departure List → **Add Item**:

- **Header name:** `TAXI`
- **Width:** 15
- **Tag item type:** `ESTaxiRoute / Taxi route`
- **Mouse button left:** `ESTaxiRoute / Edit taxi route`

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
| Qualquer outra coisa (`HS`, `VIA`…) | aparece no tag, ignorado no desenho |

O tag mostra até 15 caracteres (o limite do EuroScope); rotas maiores aparecem
com `+` no fim, mas ficam salvas inteiras (veja abrindo o editor).

A rota é apagada automaticamente quando o tráfego desconecta ou passa de
80 kt (decolou). O desenho só aparece com a aeronave abaixo de 50 kt, a até
10 NM de um aeroporto cadastrado no arquivo. A rota do tráfego selecionado
(ASEL) é desenhada mais grossa.

### Comandos

Digite na linha de comando do EuroScope:

| Comando | Ação |
|---|---|
| `.taxi A B C 27L` | define a rota do ASEL |
| `.taxi clear` | apaga a rota do ASEL |
| `.taxi show all` / `asel` / `off` | desenha todas / só a do ASEL / nenhuma |
| `.taxi color 255 200 0` | cor do desenho |
| `.taxi annot 7` / `off` | compartilha a rota com outros controladores (ver abaixo) |
| `.taxi debug` | mostra as taxiways carregadas (para conferir o arquivo) |
| `.taxi reload` | recarrega `ESTaxiRoute.txt` |
| `.taxi rec SBGR A` | começa a gravar a taxiway A clicando no radar |
| `.taxi recpt SBGR H1` | grava um ponto com um clique |
| `.taxi undo` / `save` / `cancel` | desfaz o último clique / salva / cancela a gravação |
| `.taxi help` | lista os comandos |

`show`, `color` e `annot` ficam salvos nas configurações do EuroScope.

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
```

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
