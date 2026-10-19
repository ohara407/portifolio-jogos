# Labirinto 3D

Jogo 3D em primeira pessoa que roda no terminal, escrito em C99 só com a biblioteca padrão.

![Labirinto 3D](../../docs/img/labirinto-3d.png)

Ache os três cristais espalhados pelo labirinto. Quando o último for pego, a porta laranja fica verde e abre. Passe por ela para escapar.

## Controles

| Tecla | Ação |
|---|---|
| W / S ou setas | Andar para frente e para trás |
| A / D ou setas | Girar |
| Q / E | Passo lateral |
| M | Mostra ou esconde o minimapa |
| X | Sair |

## Como o 3D funciona

A imagem é feita com raycasting. Para cada uma das 80 colunas da tela, o jogo lança um raio a partir do jogador e anda pela grade do mapa com o algoritmo DDA até bater numa parede. Quanto mais longe a parede, mais baixa ela é desenhada e mais escura fica.

Cada caractere da tela usa o bloco `▀` com cor de frente e cor de fundo diferentes, o que dobra a resolução vertical: 80 × 21 caracteres viram 80 × 42 pixels.

Os cristais são sprites. Cada coluna guarda a distância da parede (z-buffer), e o cristal só é desenhado onde está mais perto que a parede.

## Compilar

Linux e macOS:

```sh
gcc -std=c99 -O2 -o labirinto3d labirinto3d.c -lm
./labirinto3d
```

Windows (MinGW):

```sh
gcc -std=c99 -O2 -o labirinto3d.exe labirinto3d.c
labirinto3d.exe
```

No Windows, use o Windows Terminal. A janela precisa ter pelo menos 80 colunas por 24 linhas.
