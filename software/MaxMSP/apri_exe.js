const { exec } = require('child_process');
const maxAPI = require('max-api');

maxAPI.addHandler('apri', (percorsoGranulatore) => {
    // 1. Definiamo il percorso esatto di loopMIDI sul tuo computer
    const percorsoLoopMIDI = `C:/Program Files (x86)/Tobias Erichsen/loopMIDI/loopMIDI.exe`;

    maxAPI.post('Fase 1: Avvio di loopMIDI in corso...');
    
    // 2. Diciamo a Windows di lanciare loopMIDI
    exec(`cmd /c start "" "${percorsoLoopMIDI}"`, (err) => {
        if (err) {
            maxAPI.post(`Errore loopMIDI: ${err.message}`, maxAPI.POST_LEVEL.ERROR);
        } else {
            maxAPI.post('loopMIDI avviato! Attendo un secondo...');
            
            // 3. Aspettiamo 1 secondo (1000 ms) per far respirare il sistema, poi lanciamo il granulatore
            setTimeout(() => {
                maxAPI.post('Fase 2: Avvio di EmissionControl2 in corso...');
                
                exec(`cmd /c start "" "${percorsoGranulatore}"`, (errGran) => {
                    if (errGran) {
                        maxAPI.post(`Errore Granulatore: ${errGran.message}`, maxAPI.POST_LEVEL.ERROR);
                    } else {
                        maxAPI.post('Tutti i programmi sono avviati con successo!');
                    }
                });
            }, 4000);
        }
    });
});