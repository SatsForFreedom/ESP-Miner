import { Component, ViewChild, AfterViewInit } from '@angular/core';
import { AsyncPipe } from '@angular/common';
import { FormGroup } from '@angular/forms';
import { Observable } from 'rxjs';
import { SatsForFreedomComponent } from '../satsforfreedom/satsforfreedom.component';
import { EditComponent } from '../edit/edit.component';

@Component({
    selector: 'app-settings',
    templateUrl: './settings.component.html',
    standalone: true,
    imports: [AsyncPipe, EditComponent, SatsForFreedomComponent]
})
export class SettingsComponent implements AfterViewInit {
  form$!: Observable<FormGroup | null>;

  @ViewChild(EditComponent) editComponent!: EditComponent;

  constructor() {}

  ngAfterViewInit() {
    this.form$ = this.editComponent.form$;
  }
}
