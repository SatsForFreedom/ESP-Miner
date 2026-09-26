import { Component, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { first } from 'rxjs';
import { LiveDataService } from '../../services/live-data.service';
import { SystemApiService } from '../../services/system.service';

@Component({
  selector: 'app-satsforfreedom',
  standalone: true,
  imports: [CommonModule, FormsModule],
  template: `
    @if (info$ | async; as info) {
      @if (info.ASICModel === 'BM1397') {
        <section class="card mt-4">
          <h2>SatsForFreedom</h2>
          <p>Board {{ info.boardVersion }} · Actual frequency {{ info.actualFrequency | number:'1.0-1' }} MHz</p>
          <p>Mean efficiency: {{ info.meanEfficiency | number:'1.1-2' }} W/TH/s</p>
          <p>Pool session: {{ info.sessionId || 'Unavailable' }}</p>
          <p>The frequency in Settings is the upper limit. Power limiting adjusts the running frequency automatically.</p>
          <label for="sff-power">Power limit (mW)</label>
          <input id="sff-power" type="number" min="0" max="15000" step="1"
            [ngModel]="draft ?? info.powerLimitMilliwatts ?? 0" (ngModelChange)="draft = $event">
          <p>0 disables the limit; board 2.A uses 12,000 mW when set to 0.</p>
          <button type="button" (click)="save()" [disabled]="saving || draft === null">Save power limit</button>
          <p role="status">{{ message }}</p>
        </section>
      }
    }
  `
})
export class SatsForFreedomComponent {
  readonly info$ = inject(LiveDataService).info$;
  private readonly api = inject(SystemApiService);
  draft: number | null = null;
  saving = false;
  message = '';

  save(): void {
    if (this.draft === null || !Number.isInteger(this.draft) || this.draft < 0 || this.draft > 15000) {
      this.message = 'Enter a whole number between 0 and 15,000 mW.';
      return;
    }
    this.saving = true;
    this.api.updateSystem('', { powerLimitMilliwatts: this.draft }).pipe(first()).subscribe({
      next: () => { this.saving = false; this.message = 'Power limit saved.'; },
      error: () => { this.saving = false; this.message = 'Could not save the power limit.'; }
    });
  }
}
