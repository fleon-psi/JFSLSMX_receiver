
import React, {Component} from 'react';
import Slider from '@material-ui/core/Slider';
import Grid from '@material-ui/core/Grid';

class Preview extends Component {
  state = {
     contrast: 10.0,
     image: 0
  }

  componentDidMount() {
    this.interval = setInterval(() => this.updateImg(), 500);
  }

  componentWillUnmount() {
    clearInterval(this.interval);
  }

  updateImg() {
    this.setState({image: this.state.image + 1});
  }

  sliderMoved = (event, newValue) => {
    this.setState({contrast: newValue});
  };

  render() {
     return <div>
        <Grid container spacing={2}><Grid item xs={12}>
        <img src={"http://mx-jungfrau-1:5232/preview/" + this.state.contrast}/></Grid></Grid> 
        <Slider defaultValue={10.0} min={1.0} max={200} onChange={this.sliderMoved}></Slider></div>;
  }
}

export default Preview;
