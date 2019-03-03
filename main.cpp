#include <algorithm>
#include "Pokitto.h"
#include "assets.h"

// Display size: 110x88

// In the board, each cell has one byte with this
// representation (from least to most significant)
// 3 bits = color
// 1 bit  = is bomb
// 2 bits = DFS visited status (alternate bits to fast clear)
// 1 bit  = is giving combo score
// 1 bit  = is locked due to matching

// TRouBLe (255 = -1 for uint8_t)
const uint8_t directions[4][2] = {
  {255, 0}, {0, 1}, {1, 0}, {0, 255}
};

const uint8_t mask_reset_v1 = 0b11101111;
const uint8_t mask_gset_v1  = 0b00010000;

const uint8_t mask_reset_v2 = 0b11011111;
const uint8_t mask_gset_v2 =  0b00100000;

const uint8_t mask_gset_lock  =  0b10000000;
const uint8_t mask_gset_bonus =  0b01000000;


const uint8_t *sprites_pieces[] = {
  sprite_piece1,
  sprite_piece2,
  sprite_piece3,
  sprite_piece4,
  sprite_piece5,
  sprite_pieceQ,
};

const uint8_t *sprites_bombs[] = {
  sprite_bomb1,
  sprite_bomb2,
  sprite_bomb3,
  sprite_bomb4,
  sprite_bomb5,
};

const uint8_t *sprites_lock[] = {
  sprite_marked,
  sprite_combo,
};

Pokitto::Core pokitto;
uint8_t _board[77];
uint8_t *rows[11];

uint8_t restart_delay_value = 10;
uint8_t restart_freeze_value = 30;
uint8_t frozen = 0;
const uint8_t initial_delay = 20;
uint8_t remaining_delay = 20;
uint8_t remaining_shift = 8;

uint8_t column_first_hole[7];
uint8_t column_height[7];
uint8_t hold = 0;
uint8_t robot_at = 3;
uint8_t paused = 0;
uint8_t gameover = 0;
uint8_t mainmenu = 1;

int score = 0;
int highscore = 0;

struct BtnHelder {
  uint8_t frames;
  uint8_t button;

  BtnHelder(uint8_t button) {
    this->button = button;
  }

  bool operator()() {
    if (pokitto.buttons.repeat(button, 1)) {
      frames ++;
      return frames == 1 || frames == 6 || (frames >= 9 && frames & 1);
    } else {
      frames = 0;
      return false;
    }
  }
};

BtnHelder btnHelderLeft(BTN_LEFT);
BtnHelder btnHelderRight(BTN_RIGHT);

void drawPiece(uint8_t x, uint8_t y, uint8_t piece_type, const uint8_t * bitmap) {
  uint8_t c[4] = {0, 0, 0, 11};
  const uint8_t * p = &bitmap[2];
  c[1] = (piece_type << 1) + 1;
  c[2] = c[1] + 1;
  uint8_t col;
  for (uint8_t i = 0; i < (uint8_t) 8; i++, y++) {
    for (uint8_t j = 0; j < (uint8_t) 4; j++, x++, p++) {
      if ((col = c[(*p) >> 4]))
        pokitto.display.drawPixel((int16_t) x, (int16_t) y, col);
      x++;
      if ((col = c[(*p) & 0xf]))
        pokitto.display.drawPixel((int16_t) x, (int16_t) y, col);
    }
    x -= (uint8_t) 8;
  }
}

void drawPiece(uint8_t x, uint8_t y, const uint8_t * bitmap) {
  const uint8_t * p = &bitmap[2];
  uint8_t col;
  for (uint8_t i = 0; i < (uint8_t) 8; i++, y++) {
    for (uint8_t j = 0; j < (uint8_t) 4; j++, x++, p++) {
      if ((col = (*p) >> 4))
        pokitto.display.drawPixel((int16_t) x, (int16_t) y, col);
      x++;
      if ((col = (*p) & 0xf))
        pokitto.display.drawPixel((int16_t) x, (int16_t) y, col);
    }
    x -= (uint8_t) 8;
  }
}

void config() {
  pokitto.begin(); 
  pokitto.setFrameRate(30);
  srand(pokitto.getTime());
  pokitto.display.load565Palette(game_palette);
  pokitto.display.bgcolor = 0;
  pokitto.display.setFont(font3x5);
  for (int i = 0; i < 11; i += 1) {
    rows[i] = &_board[7 * i];
  }
}

void resetVisitedWDFS(uint8_t i, uint8_t j, uint8_t col) {
  rows[i][j] = (rows[i][j] & mask_reset_v1) | mask_gset_v2;
  for (uint8_t d = 0; d < 4; d++) {
    uint8_t ii = i + directions[d][0];
    uint8_t jj = j + directions[d][1];
    if (ii != 255 && jj != 255 && ii != 11 && jj != 7
      && (rows[ii][jj] & 0xf) == col
      && !(rows[ii][jj] & mask_gset_v2)) {
      resetVisitedWDFS(ii, jj, col);
    }
  }
}

uint8_t _countConnEq(uint8_t i, uint8_t j, uint8_t col) {
  uint8_t conn = 1;
  rows[i][j] = (rows[i][j] & mask_reset_v2) | mask_gset_v1;
  for (uint8_t d = 0; d < 4; d++) {
    uint8_t ii = i + directions[d][0];
    uint8_t jj = j + directions[d][1];
    if (ii != 255 && jj != 255 && ii != 11 && jj != 7
      && (rows[ii][jj] & 0xf) == col
      && !(rows[ii][jj] & mask_gset_v1)) {
      conn += _countConnEq(ii, jj, col);
    }
  }
  return conn;
}

uint8_t countConnEq(uint8_t i, uint8_t j, uint8_t col) {
  uint8_t ret = _countConnEq(i, j, col);
  resetVisitedWDFS(i, j, col);
  return ret;
}

uint8_t lockPieces(uint8_t i, uint8_t j, uint8_t col, uint8_t acc = 0) {
  acc++;
  rows[i][j] |= mask_gset_lock;
  if (acc > 4) {
    rows[i][j] |= mask_gset_bonus;
  }
  for (uint8_t d = 0; d < 4; d++) {
    uint8_t ii = i + directions[d][0];
    uint8_t jj = j + directions[d][1];
    if (ii != 255 && jj != 255 && ii != 11 && jj != 7
      && (rows[ii][jj] & 0xf) == col
      && !(rows[ii][jj] & mask_gset_lock)) {
      acc = lockPieces(ii, jj, col, acc);
    }
  }
  return acc;
}

void tryEarnAt(uint8_t i, uint8_t j) {
  uint8_t val = rows[i][j];
  if (val & 0b1000) {
    if (countConnEq(i, j, val & 0xf) > 1) {
      lockPieces(i, j, val & 0xf, 0);
      uint8_t typ = val & 0b0111;
      for (int it = 0; it < 77; it++) {
        if ((_board[it] & 0xf) == typ) {
          _board[it] |= mask_gset_lock;
        }
      }
      if (!frozen) {
        frozen = 1;
        remaining_delay = restart_freeze_value;
      }
    }
  } else {
    if (countConnEq(i, j, val & 0xf) >= 4) {
      if (!frozen) {
        frozen = 1;
        remaining_delay = restart_freeze_value;
        lockPieces(i, j, val & 0xf, 0);
      } else {
        lockPieces(i, j, val & 0xf, 4);
      }
    }
  }
}

template<typename T> void shuffle (T* v, uint8_t n) {
  for (int i = n - 1; i; i--) {
    int j = random(0, i + 1);
    std::swap(v[i], v[j]);
  }
}

bool _generateRow(uint8_t cur = 0) {
  if (cur == 7) {
    return true;
  }
  uint8_t tries[5] = {1, 2, 3, 4, 5};
  shuffle(tries, 5);
  for (int t = 0; t < 5; t++) {
    rows[0][cur] = tries[t];
    if (countConnEq(0, cur, tries[t]) < 4) {
      if (_generateRow(cur + 1)) {
        return true;
      }
    }
  }
  rows[0][cur] = 0;
  return false;
}

void generateRow() {
  for (int i = 0; i < 7; i++) {
    rows[0][i] = 0;
  }
  _generateRow();
}

void generateBomb() {
  if (random(0, 3) == 0) {
    int j = random(0, 7);
    if (countConnEq(0, j, rows[0][j] | 0b1000) == 1) {
      rows[0][j] |= 0b1000;
    }
  }
}

void drawPlayer() {
  pokitto.display.setColor(14);
  pokitto.display.fillRectangle(0, 79, 110, 0);

  pokitto.display.setColor(15);
  pokitto.display.fillRectangle(10 + 8 * robot_at, 0, 8, 88);

  pokitto.display.drawBitmap(6 + 8 * robot_at, 80, sprite_grabber);

  if (hold) {
    if (hold & 0b1000) {
      drawPiece(10 + 8 * robot_at, 83, sprites_bombs[(hold & 0b111) - 1]);
    } else {
      drawPiece(10 + 8 * robot_at, 83, sprites_pieces[(hold & 0b111) - 1]);
    }
  }
}

void drawBoard() {
  for (int col = 0; col < 7; col++) {
    for (int row = 0; row < 11; row++) {
      uint8_t val = rows[row][col];
      if (val) {
        if (paused) val = 6;
        if (val & mask_gset_lock) {
          drawPiece(10 + 8 * col, row * 8 - remaining_shift - 8, (val & 0b111) - 1, sprites_lock[(val >> 6) & 1]);
        } else if (val & 0b1000) {
          drawPiece(10 + 8 * col, row * 8 - remaining_shift - 8, sprites_bombs[(val & 0b111) - 1]);
        } else {
          drawPiece(10 + 8 * col, row * 8 - remaining_shift - 8, sprites_pieces[(val & 0b111) - 1]);
        }
      }
    }
  }
}

void rotateRows() {
  for (int i = 10; i; i--) {
    std::swap(rows[i], rows[i - 1]);
  }
}

void restartBoard() {
  for (int i = 0; i < 11*7; i++) {
    _board[i] = 0;
  }
  for (int i = 0; i < 4; i++) {
    rotateRows();
    generateRow();
  }
  robot_at = 3;
  hold = 0;
  score = 0;
  remaining_delay = initial_delay;
  remaining_shift = 8;
  gameover = 0;
  paused = 0;
  mainmenu = 0;
}

void checkInput() {
  if (gameover) {
    if (pokitto.buttons.pressed(BTN_C)) {
      highscore = max(score, highscore);
      mainmenu = 1;
    }
    return;
  } else if (paused) {
    if (pokitto.buttons.pressed(BTN_C)) {
      paused = 0;
    }
    return;
  }
  if (btnHelderRight() && robot_at != 6) {
    robot_at++;
  }
  if (btnHelderLeft() && robot_at != 0) {
    robot_at--;
  }
  if (pokitto.buttons.pressed(BTN_B)) {
    int i;
    for (i = 10; i && rows[i][robot_at] == 0; i--);
    if (i && !(rows[i][robot_at] & mask_gset_lock)
        && !(rows[i - 1][robot_at] & mask_gset_lock)) {
      std::swap(rows[i][robot_at], rows[i - 1][robot_at]);
      tryEarnAt(i - 1, robot_at);
      tryEarnAt(i, robot_at);
    }
  } else if (pokitto.buttons.pressed(BTN_A)) {
    uint8_t i;
    for (i = 10; i != 255 && rows[i][robot_at] == 0; i--);
    if (hold) {
      i++;
      if (i != 11) {
        rows[i][robot_at] = hold;
        hold = 0;
        tryEarnAt(i, robot_at);
      }
    } else {
      if (i != 255 && !(rows[i][robot_at] & mask_gset_lock)) {
        hold = rows[i][robot_at];
        rows[i][robot_at] = 0;
      }
    }
  } else if (pokitto.buttons.pressed(BTN_C)) {
    paused = 1;
  }
}

void compactBoard() {
  for (int j = 0; j < 7; j++) {
    column_first_hole[j] = 11;
    for (int i = 0; i < 11; i++) {
      if (rows[i][j] == 0) {
        column_first_hole[j] = i;
        break;
      }
    }
    column_height[j] = column_first_hole[j];

    for (int i = column_height[j]; i < 11; i++) {
      if (rows[i][j] != 0) {
        rows[column_height[j]][j] = rows[i][j];
        column_height[j]++;
        rows[i][j] = 0;
      }
    }
  }
}

void finishFrozen() {
  for (int i = 0; i < 77; i++) {
    if (_board[i] >> 7) {
      score += 10 + ((_board[i] & mask_gset_bonus) ? 10 : 0);
      _board[i] = 0;
    }
  }

  compactBoard();
  for (int j = 0; j < 7; j++) {
    for (int i = column_first_hole[j]; i < column_height[j]; i++) {
      if (!(rows[i][j] & 0b1000) && !(rows[i][j] >> 6)) {
        tryEarnAt(i, j);
      }
    }
  }

  for (int j = 0; j < 7; j++) {
    for (int i = column_first_hole[j]; i < column_height[j]; i++) {
      if ((rows[i][j] & 0b1000) && !(rows[i][j] >> 6)) {
        tryEarnAt(i, j);
      }
    }
  }

  bool keepFrozen = false;
  for (int i = 0; i < 77; i++) {
    if (_board[i] >> 7) {
      keepFrozen = true;
      break;
    }
  }

  if (keepFrozen) {
    remaining_delay = restart_freeze_value;
  } else {
    remaining_delay = restart_delay_value;
    frozen = 0;
  }

  // highscore = max(score, highscore);
}

bool checkGameOver() {
  for (int j = 0; j < 7; j++) {
    if (rows[10][j]) return true;
  }
  return false;
}

void ingame() {
  checkInput();

  if (!gameover && !paused) {
    remaining_delay--;
  }
  if (!remaining_delay && !frozen) {
    remaining_delay = restart_delay_value;
    remaining_shift--;
  } else if(!remaining_delay && frozen) {
    finishFrozen();
  }
  if (!remaining_shift) {
    if (checkGameOver()) {
      gameover = 1;
    } else {
      remaining_shift = 8;
      rotateRows();
      generateRow();
      generateBomb();
    }
  }

  pokitto.display.setColor(1);
  pokitto.display.setCursor(76, 10);
  pokitto.display.print("Score:");
  pokitto.display.setCursor(76, 18);
  pokitto.display.print(score);
  pokitto.display.setColor(3);
  pokitto.display.setCursor(76, 26);
  pokitto.display.print("HScore:");
  pokitto.display.setCursor(76, 34);
  pokitto.display.print(highscore);
  pokitto.display.setCursor(76, 44);

  if (!paused && !gameover) {
    drawPlayer();
  } else if(gameover) {
    pokitto.display.setColor(1);
    pokitto.display.setCursor(0, 82);
    pokitto.display.print("  GAME OVER           (PRESS C)");
  } else {
    pokitto.display.setColor(5);
    pokitto.display.setCursor(0, 82);
    pokitto.display.print("  PAUSED              (PRESS C)");
  }
  drawBoard();
}

char str_highscore[10];
void inmainmenu() {
  pokitto.display.setFont(fontAdventurer);
  pokitto.display.setColor(1);
  pokitto.display.setCursor(30, 10);
  pokitto.display.print("Raquer");
  pokitto.display.setCursor(45, 30);
  pokitto.display.print("Mete");
  pokitto.display.setColor(5);
  pokitto.display.setCursor(32, 32);
  pokitto.display.print("*");

  pokitto.display.setFont(fontKoubit);
  pokitto.display.setCursor(25, 50);
  pokitto.display.setColor(3);
  pokitto.display.print("High score");
  pokitto.display.setFont(fontDonut);
  pokitto.display.setColor(5);
  pokitto.display.setCursor(22, 58);
  sprintf(str_highscore, "%9d", highscore);
  for (int i = 0; str_highscore[i]; i++) {
    if (str_highscore[i] == ' ') str_highscore[i] = '-';
  }
  pokitto.display.print(str_highscore);

  pokitto.display.setFont(font3x5);
  pokitto.display.setColor(11);
  pokitto.display.setCursor(25, 82);
  pokitto.display.write("PRESS    TO PLAY");
  pokitto.display.setColor(7);
  pokitto.display.setCursor(49, 82);
  pokitto.display.write("C");

  if (pokitto.buttons.pressed(BTN_C)) {
    restartBoard();
  }
}

int main() {
  config();

  while (pokitto.isRunning()) {
    if (pokitto.update()) {
      pokitto.display.clear();

      if (mainmenu) {
        inmainmenu();
      } else {
        ingame();
      }
    }
  }

  return 0;
}