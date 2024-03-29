#include "Alquerque.h"
#include "ui_Alquerque.h"

#include <QDebug>
#include <QMessageBox>
#include <QActionGroup>
#include <QSignalMapper>

Alquerque::Player state2player(Hole::State state) {
    switch (state) {
        case Hole::RedState:
            return Alquerque::RedPlayer;
        case Hole::BlueState:
            return Alquerque::BluePlayer;
        default:
            Q_UNREACHABLE();
    }
}

Alquerque::Player otherPlayer(Alquerque::Player player) {
    return (player == Alquerque::RedPlayer ?
                    Alquerque::BluePlayer : Alquerque::RedPlayer);
}

Hole::State player2state(Alquerque::Player player) {
    return player == Alquerque::RedPlayer ? Hole::RedState : Hole::BlueState;
}

Alquerque::Alquerque(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::Alquerque),
      m_player(Alquerque::RedPlayer),
      m_mode(MovingMode),
      m_selected(nullptr),
      m_sequence(false) {

    ui->setupUi(this);

    QObject::connect(ui->actionNew, SIGNAL(triggered(bool)), this, SLOT(reset()));
    QObject::connect(ui->actionQuit, SIGNAL(triggered(bool)), qApp, SLOT(quit()));
    QObject::connect(ui->actionAbout, SIGNAL(triggered(bool)), this, SLOT(showAbout()));

    QSignalMapper* map = new QSignalMapper(this);
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 5; ++col) {
            QString holeName = QString("hole%1%2").arg(row).arg(col);
            Hole* hole = this->findChild<Hole*>(holeName);
            Q_ASSERT(hole != nullptr);

            m_board[row][col] = hole;

            int id = row * 5 + col;
            map->setMapping(hole, id);
            QObject::connect(hole, SIGNAL(clicked(bool)), map, SLOT(map()));
        }
    }
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    QObject::connect(map, SIGNAL(mapped(int)), this, SLOT(play(int)));
#else
    QObject::connect(map, SIGNAL(mappedInt(int)), this, SLOT(play(int)));
#endif

    // When the turn ends, switch the player.
    QObject::connect(this, SIGNAL(turnEnded()), this, SLOT(switchPlayer()));
    QObject::connect(this, SIGNAL(gameOver(Alquerque::Player)), this, SLOT(showGameOver(Alquerque::Player)));
    QObject::connect(this, SIGNAL(gameOver(Alquerque::Player)), this, SLOT(reset()));

    this->reset();

    this->adjustSize();
    this->setFixedSize(this->size());
}

Alquerque::~Alquerque() {
    delete ui;
}

Hole* Alquerque::neighboor(Hole* hole, Hole::Direction dir) {
    if (hole == nullptr)
        return nullptr;

    int row = -1, col = -1;
    switch (dir) {
        case Hole::North:
            row = hole->row() - 1;
            col = hole->col();
            break;
        case Hole::NorthEast:
            row = hole->row() - 1;
            col = hole->col() + 1;
            break;
        case Hole::East:
            row = hole->row();
            col = hole->col() + 1;
            break;
        case Hole::SouthEast:
            row = hole->row() + 1;
            col = hole->col() + 1;
            break;
        case Hole::South:
            row = hole->row() + 1;
            col = hole->col();
            break;
        case Hole::SouthWest:
            row = hole->row() + 1;
            col = hole->col() - 1;
            break;
        case Hole::West:
            row = hole->row();
            col = hole->col() - 1;
            break;
        case Hole::NorthWest:
            row = hole->row() - 1;
            col = hole->col() - 1;
            break;
        default:
            Q_UNREACHABLE();
    }

    return ((row >= 0 && row < 5 && col >= 0 && col < 5) ?
                m_board[row][col] : nullptr);
}

Hole* Alquerque::move(Hole* src, Hole::Direction dir, bool fake) {
    Hole::State playerState = player2state(m_player);
    if (src == nullptr || src->state() != playerState)
        return nullptr;

    Hole* dst = this->neighboor(src, dir);
    if (dst == nullptr || dst->state() != Hole::EmptyState)
        return nullptr;


    if (!fake) {
        Q_ASSERT(src->isMarked() && dst->isMarked());

        src->setState(Hole::EmptyState);
        src->setMarked(false);

        dst->setState(playerState);
    }

    return dst;
}

QList<Hole::Direction> Alquerque::moveables(Hole* src) {
    QList<Hole::Direction> moveables;

    if (src != nullptr &&
            src->state() == player2state(m_player)) {
        foreach (Hole::Direction dir, src->moves()) {
            Hole* dst = this->move(src, dir, true);
            if (dst != nullptr)
                moveables << dir;
        }
    }

    return moveables;
}

Hole* Alquerque::eat(Hole* src, Hole::Direction dir, bool fake) {
    Hole::State playerState = player2state(m_player);
    if (src == nullptr || src->state() != playerState)
        return nullptr;

    Hole* step = this->neighboor(src, dir);
    if (step == nullptr || step->state() != player2state(otherPlayer(m_player)))
        return nullptr;

    Hole* dst = this->neighboor(step, dir);
    if (dst == nullptr || dst->state() != Hole::EmptyState)
        return nullptr;

    // If it is a real move, make it.
    if (!fake) {
        Q_ASSERT(src->isMarked() && dst->isMarked());

        src->setState(Hole::EmptyState);
        src->setMarked(false);

        step->setState(Hole::EmptyState);

        dst->setState(playerState);
    }

    return dst;
}

QList<Hole::Direction> Alquerque::eatables(Hole* src) {
    QList<Hole::Direction> eatables;

    if (src != nullptr &&
            src->state() == player2state(m_player)) {
        foreach (Hole::Direction dir, src->moves()) {
            Hole* dst = this->eat(src, dir, true);
            if (dst != nullptr)
                eatables << dir;
        }
    }

    return eatables;
}

Hole::Direction Alquerque::findDirection(Hole* hole) {
    Q_ASSERT(m_selected != nullptr);
    Q_ASSERT(hole != nullptr && hole->state() == Hole::EmptyState);

    // Find the correct direction of the selected hole.
    foreach (Hole::Direction dir, m_movements[m_selected]) {
        Hole* dst;
        switch (m_mode) {
            case Alquerque::EatingMode:
                // Vérifiez si nous pouvons le manger.
                dst = this->eat(m_selected, dir, true);

                // Si c'est la bonne direction, mange-la.
                if (dst == hole)
                    return dir;

                break;
            case Alquerque::MovingMode:
                // Vérifiez si nous pouvons le déplacer.
                dst = this->move(m_selected, dir, true);

                // Si c'est la bonne direction, déplacez-le.
                if (dst == hole)
                    return dir;

                break;
            default:
                Q_UNREACHABLE();
                break;
        }
    }

    Q_UNREACHABLE();
    return Hole::North;
}

void Alquerque::select(Hole* hole) {
    Q_ASSERT(m_selected == nullptr);
    Q_ASSERT(hole != nullptr && hole->state() == player2state(m_player));

    // Sélectionnez maintenant le nouveau trou.
    m_selected = hole;
    m_selected->setMarked(true);
    foreach (Hole::Direction dir, m_movements[m_selected]) {
        Hole* dst = (m_mode == Alquerque::EatingMode ?
                        this->eat(m_selected, dir, true) : this->move(m_selected, dir, true));
        Q_ASSERT(dst != nullptr);
        dst->setMarked(true);
    }
}

void Alquerque::deselect() {
    Q_ASSERT(m_selected != nullptr);

    m_selected->setMarked(false);
    foreach (Hole::Direction dir, m_movements[m_selected]) {
        Hole* dst = (m_mode == Alquerque::EatingMode ?
                        this->eat(m_selected, dir, true) : this->move(m_selected, dir, true));
        Q_ASSERT(dst != nullptr);
        dst->setMarked(false);
    }

    m_selected = nullptr;
}

bool Alquerque::nextSequence(Hole* hole) {
    Q_ASSERT(hole != nullptr && hole->state() == player2state(m_player));

    QList<Hole::Direction> eatables = this->eatables(hole);
    if (eatables.isEmpty())
        return false;

    // Refaites la prélecture.
    bool valid = this->preplay();
    Q_ASSERT(valid);

    // Refaites la prélecture.
    Q_ASSERT(m_mode == Alquerque::EatingMode);
    Q_ASSERT(m_movements.contains(hole));
    this->select(hole);

   // Et marquez-le comme une séquence.
    m_sequence = true;

    return true;
}

bool Alquerque::preplay() {
    // Compte les joueurs.
    int count = 0;

    // Récupère l'état et le nombre du joueur.
    Hole::State playerState = player2state(m_player);

   // Réinitialise le tableau et vérifie les mouvements.
    QMap<Hole*, QSet<Hole::Direction>> eatables;
    QMap<Hole*, QSet<Hole::Direction>> moveables;
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 5; ++col) {
            Hole* hole = m_board[row][col];
            hole->setMarked(false);
            if (hole->state() == playerState) {
                hole->setEnabled(false);
                count++;
            } else {
                hole->setEnabled(true);
            }

            QList<Hole::Direction> directions;
            if ((directions = this->eatables(hole)).count() > 0) {
                foreach (Hole::Direction dir, directions)
                    eatables[hole] << dir;
            } else if ((directions = this->moveables(hole)).count() > 0) {
                foreach (Hole::Direction dir, directions)
                    moveables[hole] << dir;
            }
        }
    }

    // Vérifiez s'il ne reste plus de mouvements.
    if (count == 0)
        return false;

   // Mettre à jour les mouvements et le mode.
    if (!eatables.empty()) {
        m_movements = eatables;
        m_mode = Alquerque::EatingMode;
    } else {
        m_movements = moveables;
        m_mode = Alquerque::MovingMode;
    }

    // Vérifiez s'il ne reste plus de mouvements.
    if (m_movements.empty())
        return false;

   // Réinitialise la sélection et la séquence.
    m_selected = nullptr;
    m_sequence = false;

    // Active les trous pouvant être sélectionnés.
    foreach (Hole* hole, m_movements.keys())
        hole->setEnabled(true);

    return true;
}

void Alquerque::play(int id) {
    Hole* hole = m_board[id / 5][id % 5];
    Hole::State playerState = player2state(m_player);

    // S'il y a une sélection en place.
    if (m_selected != nullptr) {
       // Vérifiez s'il s'agit d'une séquence, dans ce cas nous sommes obligés de continuer.
        if (m_sequence) {
            if (hole->isMarked() && hole->state() == Hole::EmptyState) {
                Hole::Direction dir = this->findDirection(hole);

                Q_ASSERT(m_mode == EatingMode);
                Hole* dst = this->eat(m_selected, dir);
                Q_ASSERT(dst != nullptr);

                if (!this->nextSequence(dst))
                    emit turnEnded();
            }
        } else {
            if (hole->isMarked()) {
                if (hole->state() != Hole::EmptyState)
                    return;

                Hole::Direction dir = this->findDirection(hole);
                Hole* dst;
                switch (m_mode) {
                    case Alquerque::EatingMode:
                        dst = this->eat(m_selected, dir);
                        Q_ASSERT(dst != nullptr);

                        if (!this->nextSequence(dst))
                            emit turnEnded();

                        break;
                    case Alquerque::MovingMode:
                        dst = this->move(m_selected, dir);
                        Q_ASSERT(dst != nullptr);

                        emit turnEnded();

                        break;
                    default:
                        Q_UNREACHABLE();
                        break;
                }
            } else if (hole->state() == playerState) {
                this->deselect();

                this->select(hole);
            }
        }
    } else {
        if (!m_movements.contains(hole))
            return;

        this->select(hole);
    }
}

void Alquerque::switchPlayer() {
    m_player = otherPlayer(m_player);

    if (this->preplay()) {
        this->updateStatusBar();
    } else {
        emit gameOver(otherPlayer(m_player));
    }
}

void Alquerque::reset() {
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 5; ++col) {
            Hole* hole = m_board[row][col];
            hole->reset();

            if (row < 2 || (row == 2 && col < 2))
                hole->setState(Hole::RedState);
            else if (row > 2 || (row == 2 && col > 2))
                hole->setState(Hole::BlueState);
        }
    }

    m_player = Alquerque::RedPlayer;

    this->preplay();

    this->updateStatusBar();
}

void Alquerque::showAbout() {
    QMessageBox::information(this, tr("À propos"), tr("Alquerque\n\n Tajani Ayoub - Taskin Semih"));
}

void Alquerque::showGameOver(Alquerque::Player player) {
    switch (player) {
        case Alquerque::RedPlayer:
            QMessageBox::information(this, tr("Gagnant"), tr("Félicitations, le joueur rouge a gagné."));
            break;
        case Alquerque::BluePlayer:
            QMessageBox::information(this, tr("Gagnant"), tr("Félicitations, le Joueur Bleu a gagné."));
            break;
        default:
            Q_UNREACHABLE();
            break;
    }
}

void Alquerque::updateStatusBar() {
    QString player(m_player == Alquerque::RedPlayer ? "Rouge" : "Bleu");
    ui->statusbar->showMessage(tr("C'est le tour du joueur %2").arg(player));
}
