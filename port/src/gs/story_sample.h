// The Story Creator's sample saga (gs/story_ui.cpp lists it with the player's own; "Make a copy to edit" saves it in
// the sagas folder). The same format as a saga file: docs/port/sagas.md. Disc characters and stages only.
static const char kStorySample[] = R"SAGA({
  "format": 1,
  "title": "Custom Saga: Rivals",
  "author": "Tenkaichi3Decomp",
  "description": "A sample saga: a rival's challenge, a legendary threat, and what happens if you lose.",
  "episodes": [
    {
      "title": "A Rival's Return",
      "description": "Vegeta has been waiting for this day.",
      "stage": 23,
      "music": 8,
      "player": [{"chara": 3}],
      "enemy": [{"chara": 31}],
      "intro": {
        "distance": 2,
        "lines": [
          {"speaker": "enemy", "text": "Kakarot! I've waited a long time for this.", "pose": "Point / taunt"},
          {"speaker": "player", "text": "Vegeta? What are you doing all the way out here?", "camera": "shoulder"},
          {"speaker": "enemy", "text": "Hahaha... You should be pleased. A low-class warrior like you...", "voice": {"story": 2, "line": 2}, "camera": "low", "move": "push", "pose": "Laugh"},
          {"speaker": "enemy", "text": "...seldom gets to play with an elite like me.", "voice": {"story": 2, "line": 3}, "camera": "medium", "effects": ["aura_on", "shake"]},
          {"speaker": "player", "text": "Heh... you never change. Alright, let's go!", "camera": "close", "pose": "Fighting stance", "effects": ["aura_on"]}
        ]
      },
      "battle": {
        "fight": true,
        "cpu": 1,
        "lines": [
          {"when": "start", "speaker": "enemy", "text": "Show me everything you've got!"},
          {"when": "enemy_health", "value": 50, "speaker": "enemy", "text": "Grr... not bad, Kakarot!", "effects": ["shake"]},
          {"when": "player_health", "value": 50, "speaker": "enemy", "text": "What's wrong, Kakarot?! You can do better than that!", "voice": {"story": 2, "line": 10}},
          {"when": "player_health", "value": 30, "speaker": "player", "text": "He's gotten a lot stronger..."}
        ]
      },
      "win": {
        "distance": 2,
        "lines": [
          {"speaker": "player", "text": "You've gotten stronger, Vegeta. That was a great fight!", "camera": "two"},
          {"speaker": "enemy", "text": "Hmph. Next time, I'll be the one left standing.", "pose": "Turn away"}
        ]
      },
      "lose": {
        "lines": [
          {"speaker": "enemy", "text": "Looks like you picked the wrong fight! With the greatest of all Saiyans, none can surpass me!", "pose": "Laugh", "voice": {"story": 2, "line": 23}},
          {"speaker": "player", "text": "Ugh... I'm not done yet!", "camera": "low"}
        ]
      },
      "next_win": "next",
      "next_lose": "retry"
    },
    {
      "title": "The Legendary Super Saiyan",
      "description": "A monstrous power rises from the mountains.",
      "stage": 26,
      "music": 12,
      "player": [{"chara": 4}, {"chara": 32}],
      "enemy": [{"chara": 133}],
      "intro": {
        "distance": 3,
        "lines": [
          {"speaker": "narrator", "text": "As the two Saiyans catch their breath, a terrible power rises from the mountains...", "camera": "wide", "move": "from_wide", "move_time": 4, "effects": ["fade_in"]},
          {"speaker": "enemy", "text": "Kakarot...", "camera": "low", "move": "rise", "pose": "Laugh", "effects": ["aura_on", "big_shake"]},
          {"speaker": "player2", "text": "That power... it can't be! The Legendary Super Saiyan!"},
          {"speaker": "player", "text": "We can't beat him alone, Vegeta. Hold him off with me!", "camera": "close", "effects": ["aura_on"]}
        ]
      },
      "battle": {
        "fight": true,
        "win": "survive",
        "survive": 60,
        "cpu": 2,
        "lines": [
          {"when": "start", "speaker": "enemy", "text": "I'll destroy everything!", "effects": ["shake"]},
          {"when": "time", "value": 30, "speaker": "player", "text": "Just a little longer!"},
          {"when": "player_switch", "speaker": "player2", "text": "Out of my way, Kakarot. Watch how the Prince does it!"},
          {"when": "player_health", "value": 25, "speaker": "enemy", "text": "Is that all? Hahaha!"}
        ]
      },
      "win": {
        "distance": 3,
        "lines": [
          {"speaker": "enemy", "text": "Kakarot... I'll come for you. Wherever you go.", "camera": "medium", "effects": ["hide"]},
          {"speaker": "narrator", "text": "Broly vanishes into the night. But the battle is far from over.", "camera": "wide", "move": "pull"}
        ]
      },
      "lose": {
        "lines": [
          {"speaker": "enemy", "text": "Kakarot... this is the end!", "camera": "low", "pose": "Laugh", "effects": ["flash", "big_shake"]}
        ]
      },
      "next_win": "next",
      "next_lose": 4
    },
    {
      "title": "The Final Clash",
      "description": "Under a ruined sky, it ends here.",
      "stage": 15,
      "music": 16,
      "player": [{"chara": 6}],
      "enemy": [{"chara": 133}],
      "intro": {
        "distance": 2,
        "lines": [
          {"speaker": "enemy", "text": "KAKAROT!!", "camera": "low", "pose": "Shout", "effects": ["flash", "big_shake", "aura_on"]},
          {"speaker": "player", "text": "This ends now, Broly. No more running!", "camera": "close", "move": "push", "effects": ["charge_on"]},
          {"speaker": "player", "text": "HAAAAAAAH!", "camera": "low", "move": "rise", "pose": "Power up", "effects": ["burst", "shake", "charge_off", "aura_on"], "wait": 2}
        ]
      },
      "battle": {
        "fight": true,
        "cpu": 3,
        "enemy_health": 80,
        "player_ki": 50,
        "lines": [
          {"when": "enemy_health", "value": 50, "speaker": "enemy", "text": "Impossible...!"},
          {"when": "enemy_health", "value": 20, "speaker": "player", "text": "Now! Everything I've got!", "effects": ["flash"]}
        ]
      },
      "win": {
        "lines": [
          {"speaker": "player", "text": "It's over...", "camera": "close"},
          {"speaker": "narrator", "text": "With Broly defeated, peace returns to the Earth. For now.", "camera": "wide", "move": "pull", "move_time": 5}
        ]
      },
      "lose": {
        "lines": [
          {"speaker": "enemy", "text": "Hahaha! You're nothing but trash!", "pose": "Laugh"}
        ]
      },
      "next_win": "end",
      "next_lose": "retry"
    },
    {
      "title": "What If: The Last Saiyan",
      "description": "Goku has fallen. Only the Prince remains.",
      "stage": 15,
      "music": 12,
      "player": [{"chara": 32}],
      "enemy": [{"chara": 133}],
      "intro": {
        "distance": 3,
        "lines": [
          {"speaker": "narrator", "text": "Goku has fallen. And now, only the Prince of all Saiyans stands against the Legendary Super Saiyan.", "camera": "wide", "move": "from_wide", "effects": ["fade_in"]},
          {"speaker": "player", "text": "Kakarot, you fool... Fine. I'll finish this myself!", "camera": "close", "effects": ["aura_on"]},
          {"speaker": "enemy", "text": "A monster? No... I am the Devil!", "camera": "low", "pose": "Laugh", "effects": ["big_shake"]},
          {"speaker": "narrator", "text": "A darker story begins... Will fate be different the second time?", "camera": "two", "move": "pull"}
        ]
      },
      "battle": {"fight": false},
      "next_win": 2
    }
  ]
}
)SAGA";
