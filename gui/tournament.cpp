#include <math.h>
#include <QDebug>
#include <QFile>
#include <QApplication>

#include "tournament.h"
#include "rrgroup.h"
#include "swissgroup.h"

Tournament::Tournament( PlayerList players, Category category,
                        Match::Type matchType, unsigned int groupSize,
                         unsigned int stagesCnt )
 : _players( players ),
   _groupSize( groupSize ),
   _stagesCnt( stagesCnt ),
   _matchType( matchType ),
   _category( category ) 
{
  _groups = new QList<Group*>[ _stagesCnt ];
  Q_CHECK_PTR( _groups );

  breakPlayers( players );
  
  connect( qApp, SIGNAL( aboutToQuit() ), this, SLOT( save() ) );
}

Tournament::Tournament( ) 
 : _groupSize( 0 ),
   _stagesCnt( 0 ),
   _matchType( Match::BestOf3 ),
   _category( M2 ) 
{
  connect( qApp, SIGNAL( aboutToQuit() ), this, SLOT( save() ) );
}

void Tournament::groupChanged( Group* g )
{
  if ( g->stage() == 0 ) {
    if ( roundRobinCompleted() ) {
      buildGroups( ); 
    }
  } else {
    if ( g->completed() && ( g->stage() != ( _stagesCnt - 1 ) ) ) {
      splitSwissGroup( dynamic_cast< SwissGroup* >( g ) ); 
    }
  }

  save();
}

/** build groups by results of round-robin stage 
 *   \todo this code should be in split() method of RRGroup
 */
void Tournament::buildGroups( )
{
  // 1st stage group size.
  // stages = 4 => gs(1) = 8
  // stages = 3 => gs(1) = 4
  int gs = 1 << ( _stagesCnt - 1 ); 
  PlayerList players = roundRobinResults( );

  newSwissGroup( 1, 1, players.mid( 0, gs ) );

  int nloosers = players.count() - gs;
  if ( nloosers <= 1 ) {
    nloosers = 0;
  } else {
    int size = gs;
    // finding such nloosers that log2( nloosers ) is integer.
    while ( size != 1 ) {
      if ( nloosers >= size ) {
        // finding such nloosers that is not greater than gs
        nloosers = size;
        break;
      }
      size = size/2;
    } 
  } 

  newSwissGroup( gs + 1, 1, players.mid( gs, nloosers ) ); 
}

/** splits groups for two groups - group of winners and group of loosers.
 */
void Tournament::splitSwissGroup( SwissGroup* g )
{
  QList< Group* > groups = g->split();   

  // if group contains only 1 match then it will not be splitted
  // and 

  if ( groups.count() ) {
    _groups[ groups.at( 0 )->stage() ] << groups;
  }

  for ( int i = 0; i < groups.count(); i ++ ) {
    emit newSwissGroupCreated( dynamic_cast< SwissGroup* >( groups.at( i ) ) );
  }
}

SwissGroup* Tournament::newSwissGroup( unsigned int fromPlace, unsigned int stage, 
                                       PlayerList players )
{
  SwissGroup* sg = new SwissGroup( fromPlace, this, stage, players );

  _groups[ 1 ] << sg;
  emit newSwissGroupCreated( sg );
  return sg;
}

/** Sorts player list and pushes cool players into first places
 * of groups. Less cool players into second places and so on.
 * Function assumes that 'players' is sorted list.
 */
void Tournament::breakPlayers( PlayerList players )
{
  int groupCnt = ceil( (double) players.count() / _groupSize );
  
  _groups[0].clear(); 

  for ( int i = 0; i < groupCnt; i ++ ) {
    _groups[0] << new RRGroup( QChar( 'A' + i ), this );
  }

  while ( players.count() ) {
    for ( int i = 0; ( i < groupCnt ) && players.count(); i ++ ) {
      Player player = players.takeLast();
     
      _groups[0][i]->addPlayer( player );
    }
  }
}

bool Tournament::roundRobinCompleted( ) const
{
  for ( int i = 0; i < _groups[ 0 ].count(); i ++ ) {
    if ( ! _groups[ 0 ].at( i )->completed() ) {
      return false;
    }
  }
 
  return true;
}

/** \return player list built by cyclic principle: 1st from 1 group,
 *          1st from 2 group, 1st from Nth group; 2nd from 1 group etc...
 */
PlayerList Tournament::roundRobinResults() const
{
  PlayerResultsList bestlist;

  // 1st and 2nd places
  for ( int p = 1; p <= (int)2; p ++ ) {
    for ( int i = 0; i < _groups[0].count(); i ++ ) {
      const Group* g = _groups[0].at( i );
      if ( p <= g->size() ) {
        Player player = g->playerByPlace( p );
        bestlist << g->playerResults( player );
      }
    }
  }

  PlayerResultsList list;
  // 3rd and more places
  for ( int p = 3; p <= _groupSize; p ++ ) {
    for ( int i = 0; i < _groups[0].count(); i ++ ) {
      const Group* g = _groups[0].at( i );
      if ( p <= g->size() ) {
        Player player = g->playerByPlace( p );
        bestlist << g->playerResults( player );
      }
    }
  }

  qSort( list.begin(), list.end(), qGreater< PlayerResults >() );

  bestlist << list;

  return toPlayerList( bestlist );
}

/** called when app exits by the signal aboutToQuit();
 */
void Tournament::save()
{
  QFile file( "tourn.dat" );
  if ( file.open( QIODevice::WriteOnly ) ) {
    QDataStream stream( & file );
    stream << (*this);
  } 
}

/** creates and instance of Tournament initialized from specified file.
 */
Tournament* Tournament::fromFile( QString fileName )
{
  QFile file( fileName );
  Tournament* t = NULL;
  if ( file.exists() && file.open( QIODevice::ReadOnly ) ) {
    t = new Tournament();

    QDataStream ds( &file );
    
    ds >> (*t); 
  } 

  return t;
}
/* serialization
 */
QDataStream &operator>>(QDataStream &s, Tournament& t)
{
  int mType, cat;
  
  s >> t._players >> t._groupSize >> t._stagesCnt 
    >> mType >> cat; 

  t._category = (Tournament::Category) cat;
  t._matchType = (Match::Type) mType;
  t._groups = new QList<Group*>[ t._stagesCnt ];

  for ( unsigned int i = 0; i < t._stagesCnt; i ++ ) {
    int count;
    s >> count;
    for ( int j = 0; j < count; j ++ ) {
      if ( i == 0 ) { // round robin stage
        RRGroup* rrg = new RRGroup();
        s >> (*rrg);
 
        rrg->setTournament( &t );
        t._groups[i] << rrg; 
      } else {
        SwissGroup* sg = new SwissGroup();
        s >> (*sg);
        
        sg->setTournament( &t );
        t._groups[i] << sg; 
      }
    }   
  }
  return s;
}

QDataStream &operator<<(QDataStream &s, const Tournament& t)
{
  s << t._players << t._groupSize << t._stagesCnt 
    << (int) t._matchType << (int) t._category; 

  for ( unsigned int i = 0; i < t._stagesCnt; i ++ ) {
    int count = t._groups[ i ].count();
    s << count;
    for ( int j = 0; j < count; j ++ ) {
      const Group* g = t._groups[ i ].at( j );
      if ( i == 0 ) { // round robin stage
        const RRGroup* rrg = dynamic_cast< const RRGroup* >( g );

        s << (*rrg);
      } else {
        const SwissGroup* sg = dynamic_cast< const SwissGroup* >( g );
         
        s << (*sg);
      }
    }   
  }
  return s;
}
